# Linux SOCD cleaner
A (less feature rich) Linux version of [SOCD cleaner](https://github.com/valignatev/socd).
Currently this only implements the basic functionality without things such as custom keybinds or 
being able to set the applications in which this works. The only keys supported right now are WASD, but it should
be trivial to make it function for arrow keys.

This was built and tested on Arch Linux, so the experience on other distributions may vary, but it should be
relatively easy to make this work.

## Running
This program requires `sudo` to run, or otherwise it won't be able to read key inputs.
First you have to build it with `./release` (if an error with permission denied shows do `chmod +x ./release` and try again) and
then you should be able to run it with `sudo ./socd`. To exit just hit `ctrl + c` in the terminal its run in.

## Possible errors
In case of any errors it would be useful if you submitted an issue with any details regarding it, but here are some which may occur:
- Some devices don't have `/dev/input/by-id/`, which is what I have primarily tested on, but it *should* also work from `/dev/input/by-path/`. I have not been able to test that. It may be that the program doesn't crash, but also just won't work as intended.
- It could also be that the program mistakenly dismisses a valid keyboard and prints `"error: Failed to get keyboards"`.

## Why do I need it
I recommend checking checking the original [SOCD cleaners](https://github.com/valignatev/socd) README for that. Note that this does not have all the features of the original and only the basic functionality is the same.

## The original version
Shout out to the original version of SOCD cleaner created by [@valignatev](https://github.com/valignatev).
Repository for the original: https://github.com/valignatev/socd

## License
This is licensed under the MIT license.
