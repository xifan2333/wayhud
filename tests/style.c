#include "style.h"
#include <assert.h>
#include <string.h>

int main(void) {
    wayhud_style_t style;
    wayhud_style_init_default(&style);
    assert(wayhud_style_parse(&style,
                             "label {font-family: \"Liberation Mono\", '方正隶书_GBK', monospace;}"
                             "label#captions {font-size: 30px;}",
                             "captions") == 0);
    assert(strcmp(style.font_family, "Liberation Mono, 方正隶书_GBK, monospace") == 0);
    assert(style.font_size == 30);
    /* Later layers override earlier instance rules without losing other properties. */
    assert(wayhud_style_parse(&style, "label {font-size: 20px;}", "captions") == 0);
    assert(style.font_size == 20);
    assert(strstr(style.font_family, "方正隶书_GBK"));
    assert(wayhud_style_parse(&style, "label {font-family: \"An\\\"other\", serif;}", NULL) == 0);
    assert(strcmp(style.font_family, "An\"other, serif") == 0);
    assert(wayhud_style_parse(&style, "label {font-family: \"unterminated;}", NULL) == 0);
    assert(strcmp(style.font_family, "An\"other, serif") == 0);
    assert(wayhud_style_parse(&style, "label {font: bold 16px \"A\", \"B\";}", NULL) == 0);
    assert(strcmp(style.font_family, "A, B") == 0);
    assert(style.font_size == 16 && style.font_weight == 700);
    return 0;
}
