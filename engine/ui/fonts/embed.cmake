# Writes INPUT's bytes as a C++ array: jm_default_font_data / _size.
file(READ ${INPUT} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(SIZE ${INPUT} size)
file(WRITE ${OUTPUT} "#include <cstddef>\n#include <cstdint>\nextern const uint8_t jm_default_font_data[] = {${bytes}};\nextern const size_t jm_default_font_size = ${size};\n")
