#ifndef APPS_H
#define APPS_H

/* days since 1970 from the phone's clock or "--day N", used for flashcard schedules */
extern int app_today;

/* each app returns 0 to go back to the menu, -1 when the system asks to quit */
int dict_main(void);
int cards_main(void);
int reader_main(void);
int notes_main(void);
int quiz_main(void);

#endif
