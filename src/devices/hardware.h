#include "../uxn.h"

typedef enum Events {
    QUIT,
    PINUP,
    PINDOWN    
} Event;

typedef struct Pin_Event {
    Events type;
} Pin_Event;

int event_queue_length(Pin_Event *e);