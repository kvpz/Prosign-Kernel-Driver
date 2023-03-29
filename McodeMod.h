/**
 *  Author: Alexander Pons
 *  Last modifier: Kevin Perez
 */
#define CQ_DEFAULT	0

/** 
   The empty string, follwed by 26 letter codes, followed by the 10 numeral codes, followed by the comma,
   period, and question mark.  


*/
#define DOT_TIME 0.5
#define DOT_TIME_MS 500
#define DASH_TIME 1.5
#define DASH_TIME_MS 1500
#define INTERCODE_TIME 0.25
#define INTERCODE_TIME_MS 250
#define INTERLETTER_TIME 2.0
#define INTERLETTER_TIME_MS 2000

char *morse_code[40] = {"",
".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",
".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",
".--","-..-","-.--","--..","-----",".----","..---","...--","....-",
".....","-....","--...","---..","----.","--..--","-.-.-.","..--.."};

inline char * mcodestring(int asciicode)
{
   char *mc;   // this is the mapping from the ASCII code into the mcodearray of strings.

   if (asciicode > 122)  // Past 'z'
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 96)  // Upper Case
      mc = morse_code[asciicode - 96];
   else if (asciicode > 90)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 64)  // Lower Case 
      mc = morse_code[asciicode - 64];
   else if (asciicode == 63)  // Question Mark
      mc = morse_code[39];    // 36 + 3 
   else if (asciicode > 57)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 47)  // Numeral
      mc = morse_code[asciicode - 21];  // 27 + (asciicode - 48) 
   else if (asciicode == 46)  // Period
      mc = morse_code[38];  // 36 + 2 
   else if (asciicode == 44)  // Comma
      mc = morse_code[37];   // 36 + 1
   else
      mc = morse_code[CQ_DEFAULT];
   return mc;
}

