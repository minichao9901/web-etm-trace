/* -nostartfiles keeps ST's reset/vector code. newlib still calls _init(). */
void _init(void) {}
void _fini(void) {}
