Raymond Lawrence #5349411
Matthew Tharp #5341531  

We implemented the project in full.

The total size of the shared memory region is determined by the following formula:
    shared_size = (size * (MAXDIRPATH + MAXKEYWORD + 2) + 3)
This size was chosen to hold  the maximum number of possible inputs, plus three extra bytes for metadata.
The data stored in the array is primarily a sequence of null-terminated strings matching the lines of the input file.
The first extra byte at the end holds an extra null character. The final two bytes hold an unsigned short called overlap.
The overlap short keeps count of the characters in an overlapping string that don't go over the buffer size.
