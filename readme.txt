SC2 - SUPER LIGHTWEIGHT SPELL CHECKER
=====================================

A spell checker that fits in a single 390 KB file and runs anywhere
Python 3 runs. No installation, no dependencies, no cloud. It reads a
text file, shows you the words it doesn't recognise, and lets you fix
them one by one.


WHY IT'S LIGHT
--------------

The dictionary holds about 88,000 English words, but it is not stored as
a list of words. It is front-coded: the words are sorted, and each word
stores only the part that differs from the one before it. "AARDVARK" is
followed by "AARDVARKS", which stores just "S" plus a note saying "reuse
the first 8 letters". On average a word costs about 1.6 bytes instead of
7, so 88,000 words fit in 390 KB instead of 3 MB.

The file also carries a small restart table: every 8th word is stored in
full, so the program can jump into the middle of the dictionary and read
forward at most 7 words to find what it is looking for. That is what
makes lookups fast despite the compression.


HOW TO RUN
----------

  python3 SC2.py dict.smd mytext.txt mytext.txt

On Windows:

  py SC2.py dict.smd mytext.txt mytext.txt

The last two arguments are input and output. Passing the same name twice
edits the file in place. To keep the original, give a different output
name.


WHAT YOU SEE
------------

For each unknown word:

  The QUICK brown [foks] jumps over teh lazy dog.

    1. fobs
    2. foes
    3. fogs
    4. fops
    5. folks
    6. more guesses (15 left)
    7. ignore
    8. replace
    q. quit and save

Type a number, press Enter.


THE KEYS
--------

  1-5    pick that guess
  6      more guesses
  7      ignore this word
  8      type your own replacement (it is checked against the dictionary)
  q      quit and save
  Enter  cancel this menu, move to the next word

The corrected text is saved after every change, so nothing is lost if
you quit or the power goes out. The original file is only replaced when
the program finishes cleanly.


LIMITS
------

- Only A-Z letters are checked. Numbers and punctuation are left alone.
- Names, places and technical terms are not in the dictionary; ignore
  them or use option 8.
- The five guesses are suggestions, not guarantees. Option 8 always
  lets you type the right word.