/**
 * mc.c
 * 
 * Character driver receives and transmits messages morse code signals
 * through the Beaglebone USR0 LED.
 *
 * Tasks pending:
 * - test empty strings written to driver
 * - test strings without terminating null char written to driver
 * 
 * Issues:
 * - mc_tx_timer does not function after first use of driver.
 *
 * Last updated: 12/5/2021 by Kevin Perez
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/ctype.h>
#include <linux/gpio.h>
#include <linux/kdev_t.h>
#include <linux/ioport.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/io.h> 
#include <linux/mutex.h>
#include <linux/delay.h> // for testing mutexes
#include <linux/platform_data/gpio-omap.h>
#include <linux/leds.h>
#include "McodeMod.h"
#include <linux/timer.h>

int DEBUG = 1;

/* Character device driver necessities */
#define DEVICE_NAME "mc"
#define CLASS_NAME "test"
static int major_number;
static char message[256] = {0};
static struct class* mc_class = NULL;
static struct device* mc_device = NULL;

/* Addresses of BBB CPU registers assigned to physical GPIO1 - see AM335x technical reference */
#define GPIO1_START_ADDRESS 0x4804C000
#define GPIO1_END_ADDRESS 0x4804CFFF
#define USR0 (1<<21) // bit 21 of GPIO1 bank corresponds to USR0 LED
static volatile unsigned int* gpio1_set_address;
static volatile unsigned int* gpio1_clear_address;
static volatile void* gpio1_address;
size_t gpio1_size = GPIO1_END_ADDRESS - GPIO1_START_ADDRESS + 1;

/* BBB USR0 LED trigger info for vfs access */
const char* trigger_file_name = "/sys/class/leds/beaglebone:green:usr0/trigger";
#define TRIGGER_NONE "none"
#define TRIGGER_HEARTBEAT "heartbeat"
static void set_trigger_none(void);
static void set_trigger_heartbeat(void);

/* Available operations for this character driver */
static int dev_open(struct inode*, struct file*);
static int dev_release(struct inode*, struct file*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);
static struct file_operations fops = {
  .open = dev_open,
  .write = dev_write,
  .release = dev_release,
};

/* timer used for regulating morse code transmissions */
static struct timer_list mc_tx_timer;
static void initialize_timer(void);

/* Functions used to work with virtual file system (vfs) */
struct file * file_open(const char * path, int flags, int rights);
int file_write(struct file * file, unsigned long long offset, unsigned char * data, unsigned int size);

/* Mutex to prevent multi-user access to this driver */
static DEFINE_MUTEX(ebbchar_mutex);

/* morse code transmission helper variables */
int message_itr = 0; // buffer message iterator
int mcode_itr = 0; // mcode iterator
bool intercode = 0; // true if in between mc code signal transmissions
int mlength = 0; // length of message
int mcodelength = 0; // length of morse code for a single character

struct file * file_open(const char * path, int flags, int rights)
{
  // reference: https://stackoverflow.com/questions/1184274/read-write-files-within-a-linux-kernel-module
  struct file * file = NULL;
  mm_segment_t oldfs;
  int err = 0;
  oldfs = get_fs();
  set_fs(get_ds());
  file = filp_open(path, flags, rights);
  set_fs(oldfs);
  if (IS_ERR(file)) {
    printk(KERN_INFO "Issue opening trigger file. ERROR: %d\n", IS_ERR(file));
    err = PTR_ERR(file);
    return NULL;
  }

  return file;
}

int file_write(struct file * file, unsigned long long offset, unsigned char * data, unsigned int size)
{
  // reference: https://stackoverflow.com/questions/1184274/read-write-files-within-a-linux-kernel-module
  int ret;
  mm_segment_t oldfs;
  oldfs = get_fs();
  set_fs(get_ds());
  ret = vfs_write(file, data, size, &offset); // returns # of characters written
  set_fs(oldfs);
  return ret;
}

static void set_trigger_none(void)
{
  struct file* f = NULL;
  f = file_open(trigger_file_name, O_WRONLY , 0444);
  file_write(f, f->f_pos, TRIGGER_NONE, 4);
  filp_close(f, NULL);
}

static void set_trigger_heartbeat(void)
{
  struct file* f = NULL;
  f = file_open(trigger_file_name, O_WRONLY , 0444);
  file_write(f, f->f_pos, TRIGGER_HEARTBEAT, 9);
  filp_close(f, NULL);
}

static void mc_tx_timer_callback(struct timer_list* timer)
{
  if (message_itr > mlength - 1) {
    // mesage transmission completed.
    // restart LED USR0 heartbeat
    set_trigger_heartbeat();
    message_itr = 0;
    mcode_itr = 0; 
    mlength = 0;
    mcodelength = 0;
    mutex_unlock(&ebbchar_mutex);
  }
  else if (mcode_itr > mcodelength - 1) {
    // Character has been transmitted.
    // Go to next character in message.
    // This handles delays for  spaces in message, too..
    mod_timer(&mc_tx_timer, jiffies + msecs_to_jiffies(INTERLETTER_TIME_MS));
    mcode_itr = 0;
    ++message_itr;
    mcodelength = strlen(mcodestring(message[message_itr]));
    *gpio1_clear_address = USR0;
  }
  else if (intercode) {
    mod_timer(&mc_tx_timer, jiffies + msecs_to_jiffies(INTERCODE_TIME_MS));
    intercode = 0;
    *gpio1_clear_address = USR0;
  }
  else {
    switch(mcodestring(message[message_itr])[mcode_itr]) {
    case '.':
      mod_timer(&mc_tx_timer, jiffies + msecs_to_jiffies(DOT_TIME_MS));
      intercode = 1;
      *gpio1_set_address = USR0;
      break;
    case '-':
      mod_timer(&mc_tx_timer, jiffies + msecs_to_jiffies(DASH_TIME_MS));
      intercode = 1;
      *gpio1_set_address = USR0;
      break;
    }

    ++mcode_itr;
  }
}

static void initialize_timer()
{
  mod_timer(&mc_tx_timer, 0);
}

static int dev_open(struct inode* inodep, struct file* filep)
{
  if (DEBUG)
    printk(KERN_INFO "mc: device has been opened\n");

  /* Dynamically initialize mutex */
  /* 
   * Enable indirect, kernel buffered I/O by mapping GPIO1
   * physical address to virtual kernel address space for memory-mapped I/O.
   * This is a critical section that requires only this thread to modify the 
   * memory location at this particular time of execution. A semaphore needs to 
   * be used to mutually exclude processes from modifying the section of memory
   * of concern, i.e. the semaphore should be a mutex. When the mutex is set to 1,
   * i.e. the mutex is unlocked, only this process will modify the memory locations; 
   * otherwise, if the mutex is set to 0, or locked, no thread will have access.
   */
  mutex_init(&ebbchar_mutex);
  if(!mutex_trylock(&ebbchar_mutex)) {
    printk(KERN_ALERT "mc_init: mutex try: device in use by another process");
    return -EBUSY;
  }
  
  return 0;
}

static ssize_t dev_write(struct file* filep, const char* buffer, size_t len, loff_t* offset)
{
  int ret;
  ret = copy_from_user(message, buffer, len);
  mlength = strlen(message);
  mcodelength = strlen(mcodestring(message[0])); // TODO: currently assuming message is not null..
  set_trigger_none(); // turn off USR0 LED
  initialize_timer();
  
  return len;
}

static int dev_release(struct inode* inodep, struct file* filep)
{
  mutex_unlock(&ebbchar_mutex);

  return 0;
}

static int __init mc_init(void)
{
  /* Dynamically allocate major number */
  major_number = register_chrdev(0, DEVICE_NAME, &fops);
  if (major_number < 0) {
    printk(KERN_ALERT "mc: failed to register a major number\n");
    return major_number;
  }
  
  /* Register the device class */
  mc_class = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(mc_class)) {
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_ALERT "mc: failed to register device class\n");
    return PTR_ERR(mc_class);
  }

  /* Register the device driver */
  mc_device = device_create(mc_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
  if (IS_ERR(mc_device)) {
    class_destroy(mc_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_ALERT "mc: failed to create the device\n");
    return PTR_ERR(mc_device);
  }

  timer_setup(&mc_tx_timer, mc_tx_timer_callback, 0);
  
  gpio1_address = ioremap(GPIO1_START_ADDRESS, gpio1_size);
  gpio1_set_address = gpio1_address + OMAP4_GPIO_SETDATAOUT; //+ 0x0194;
  gpio1_clear_address = gpio1_address + OMAP4_GPIO_CLEARDATAOUT; //+ 0x0190;
    
  return 0;
}

static void __exit mc_exit(void)
{
  mutex_destroy(&ebbchar_mutex);
  del_timer(&mc_tx_timer);
  device_destroy(mc_class, MKDEV(major_number, 0));
  class_unregister(mc_class);
  class_destroy(mc_class);
  unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(mc_init);
module_exit(mc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Perez");
MODULE_DESCRIPTION("Character driver for LED morse code transmission on Beaglebone Black");
MODULE_VERSION("0.1");


