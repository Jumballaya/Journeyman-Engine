# Writes INPUT's bytes as a C++ array named NAME (NAME_data, NAME_size) into OUTPUT.
file(READ ${INPUT} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(SIZE ${INPUT} size)
file(WRITE ${OUTPUT} "#include <cstddef>\n#include <cstdint>\nextern const uint8_t ${NAME}_data[] = {${bytes}};\nextern const size_t ${NAME}_size = ${size};\n")
