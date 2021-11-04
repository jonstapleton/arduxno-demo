typedef enum Events {
    QUIT,
    PINUP,
    PINDOWN    
} Event;


typedef struct Hardware {
    unsigned int state:8;
    int pins[8];
} Hardware;

typedef struct Pin_Event {
    Event type;
    unsigned int snapshot:8;
} Pin_Event;



int is_event(Pin_Event *e);
int compare_hardware_state(Hardware *h); // returns 1 if the hardware changed, 0 if it is the same as before
int setup_hardware();
int get_pins(Hardware *h);