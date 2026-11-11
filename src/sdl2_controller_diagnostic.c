#include <SDL.h>
#include <stdio.h>

enum { DIAGNOSTIC_MAX_DEVICES = 32 };

typedef struct {
    SDL_JoystickID instance;
    SDL_GameController *gamepad;
    SDL_Joystick *joystick;
} DiagnosticDevice;

static DiagnosticDevice devices[DIAGNOSTIC_MAX_DEVICES];
static int device_count;

static DiagnosticDevice *find_device(SDL_JoystickID instance) {
    for(int i=0;i<device_count;++i)
        if(devices[i].instance==instance)return &devices[i];
    return NULL;
}

static void inspect_device(int index) {
    if(index<0 || index>=SDL_NumJoysticks())return;
    SDL_Joystick *joystick=SDL_JoystickOpen(index);
    if(!joystick) {
        printf("DEVICE index=%d joystick open failed: %s\n",index,SDL_GetError());
        return;
    }
    SDL_JoystickID instance=SDL_JoystickInstanceID(joystick);
    DiagnosticDevice *known=find_device(instance);
    if(known) {
        SDL_JoystickClose(joystick);
        return;
    }
    if(device_count>=DIAGNOSTIC_MAX_DEVICES) {
        SDL_JoystickClose(joystick);
        return;
    }

    DiagnosticDevice *device=&devices[device_count++];
    device->instance=instance;
    device->joystick=joystick;
    int mapped=SDL_IsGameController(index);
    char guid[64]="";
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick),guid,sizeof(guid));
    printf("DEVICE added index=%d instance=%d name=\"%s\" gamecontroller=%s vendor=%04X product=%04X guid=%s path=%s\n",
        index,(int)instance,SDL_JoystickName(joystick),mapped?"yes":"no",
        SDL_JoystickGetVendor(joystick),SDL_JoystickGetProduct(joystick),guid,
        SDL_JoystickPath(joystick)?SDL_JoystickPath(joystick):"?");

    if(mapped) {
        device->gamepad=SDL_GameControllerOpen(index);
        printf("  SDL_GameControllerOpen: %s",
            device->gamepad?"success":"failed");
        if(device->gamepad)
            printf(" name=\"%s\"",SDL_GameControllerName(device->gamepad));
        else printf(" error=\"%s\"",SDL_GetError());
        putchar('\n');
    }
    printf("  raw joystick: buttons=%d axes=%d hats=%d\n",
        SDL_JoystickNumButtons(joystick),SDL_JoystickNumAxes(joystick),
        SDL_JoystickNumHats(joystick));
    fflush(stdout);
}

static void remove_device(SDL_JoystickID instance) {
    DiagnosticDevice *device=find_device(instance);
    if(!device)return;
    printf("DEVICE removed instance=%d\n",(int)instance);
    if(device->gamepad)SDL_GameControllerClose(device->gamepad);
    if(device->joystick)SDL_JoystickClose(device->joystick);
    int index=(int)(device-devices);
    devices[index]=devices[--device_count];
    fflush(stdout);
}

static const char *button_name(Uint8 button) {
    switch(button) {
    case SDL_CONTROLLER_BUTTON_A:return "A";
    case SDL_CONTROLLER_BUTTON_B:return "B";
    case SDL_CONTROLLER_BUTTON_X:return "X";
    case SDL_CONTROLLER_BUTTON_Y:return "Y";
    case SDL_CONTROLLER_BUTTON_BACK:return "BACK";
    case SDL_CONTROLLER_BUTTON_GUIDE:return "GUIDE";
    case SDL_CONTROLLER_BUTTON_START:return "START";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:return "LEFTSTICK";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:return "RIGHTSTICK";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return "LEFTSHOULDER";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return "RIGHTSHOULDER";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:return "DPAD_UP";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:return "DPAD_DOWN";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:return "DPAD_LEFT";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:return "DPAD_RIGHT";
    default:return "UNKNOWN";
    }
}

static void print_event(const SDL_Event *event) {
    switch(event->type) {
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_JOYDEVICEADDED:
        inspect_device(event->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_JOYDEVICEREMOVED:
        remove_device(event->cdevice.which);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        printf("GAMECONTROLLER instance=%d button=%u (%s) %s\n",
            (int)event->cbutton.which,event->cbutton.button,
            button_name(event->cbutton.button),
            event->type==SDL_CONTROLLERBUTTONDOWN?"down":"up");
        break;
    case SDL_CONTROLLERAXISMOTION:
        printf("GAMECONTROLLER instance=%d axis=%u value=%d\n",
            (int)event->caxis.which,event->caxis.axis,event->caxis.value);
        break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        printf("JOYSTICK instance=%d button=%u %s\n",
            (int)event->jbutton.which,event->jbutton.button,
            event->type==SDL_JOYBUTTONDOWN?"down":"up");
        break;
    case SDL_JOYAXISMOTION:
        printf("JOYSTICK instance=%d axis=%u value=%d\n",
            (int)event->jaxis.which,event->jaxis.axis,event->jaxis.value);
        break;
    case SDL_JOYHATMOTION:
        printf("JOYSTICK instance=%d hat=%u value=0x%02X\n",
            (int)event->jhat.which,event->jhat.hat,event->jhat.value);
        break;
    default:
        break;
    }
    fflush(stdout);
}

int main(int argc,char **argv) {
    SDL_version version;
    SDL_GetVersion(&version);
    printf("SDL %u.%u.%u\n",version.major,version.minor,version.patch);
    if(SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK)<0) {
        fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());
        return 1;
    }
    int count=SDL_NumJoysticks();
    printf("SDL_NumJoysticks: %d device(s)\n",count);
    for(int index=0;index<count;++index)inspect_device(index);
    if(argc>1 && SDL_strcmp(argv[1],"--scan-only")==0) {
        SDL_Quit();
        return 0;
    }
    puts("Listening for controller input. Press buttons, move sticks/triggers; Ctrl-C exits.");
    fflush(stdout);
    for(;;) {
        SDL_Event event;
        while(SDL_PollEvent(&event))print_event(&event);
        SDL_Delay(5);
    }
}
