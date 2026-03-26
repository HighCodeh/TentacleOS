# FreeType Example

This is a simple example of initializing FreeType library, loading a font from a filesystem, and rendering a line of text.

The font file (DejaVu Sans) is downloaded at compile time and is added into a SPIFFS filesystem image. The filesystem is flashed to the board together with the application. The example loads the font file and renders "FreeType" text into the console as ASCII art.

This example doesn't require any special hardware and can run on any development board.

## Building and running

Run the application as usual for an ESP-IDF project. For example, for ESP32:
```
idf.py set-target esp32
idf.py -p PORT flash monitor
```

## Example output

The example should output the following:

```
I (468) main_task: Calling app_main()
I (538) example: FreeType library initialized
I (1258) example: Font loaded
I (1268) example: Rendering char: 'F'
I (1388) example: Rendering char: 'r'
I (1528) example: Rendering char: 'e'
I (1658) example: Rendering char: 'e'
I (1798) example: Rendering char: 'T'
I (1938) example: Rendering char: 'y'
I (2078) example: Rendering char: 'p'
I (2208) example: Rendering char: 'e'


######.                          #########
##                                  +#
##      #####   +###+    +###+      +#   +#    ########     +###+
##      ##+    +#. .#+  +#. .#+     +#    #+   #+##+ .#+   +#. .#+
######  ##     #+   +#  #+   +#     +#    ##  +# ##   +#   #+   +#
##      ##    .####### .#######     +#    .#  ## ##   .#  .#######
##      ##    .#.      .#.          +#     #+.#. ##   .#  .#.
##      ##     #+       #+          +#     +###  ##   +#   #+
##      ##     ##+  ++  ##+  ++     +#      ##+  ##+ .#+   ##+  ++
##      ##      +####    +####      +#      ##   ######     +####
                                            ##   ##
                                           +#.   ##
                                          ##+    ##



```
