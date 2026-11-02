#ifndef VI_KEYBOARD_H
#define VI_KEYBOARD_H
void vi_key_init(int ansi);
void vi_key_exit(void);
int vi_key_concurrent(void);
int vi_key_read(void);
#ifdef __RETRO80__
#define VI_UP 256
#define VI_DOWN 257
#define VI_LEFT 258
#define VI_RIGHT 259
#define VI_HOME 260
#define VI_END 261
#define VI_PGUP 262
#define VI_PGDOWN 263
#define VI_INSERT 264
#define VI_DELETE 265
void vi_key_unread(int c);
#endif
#endif
