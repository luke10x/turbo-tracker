#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum { DIAGNOSTIC_MAX_DEVICES = 32 };

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gamepad;
    SDL_Joystick *joystick;
} DiagnosticDevice;

static DiagnosticDevice devices[DIAGNOSTIC_MAX_DEVICES];
static int device_count;

static DiagnosticDevice *find_device(SDL_JoystickID id) {
    for(int i=0;i<device_count;++i)
        if(devices[i].id==id)return &devices[i];
    return NULL;
}

static const char *yes_no(bool value) {
    return value?"yes":"no";
}

static void inspect_device(SDL_JoystickID id) {
    if(find_device(id) || device_count>=DIAGNOSTIC_MAX_DEVICES)return;
    DiagnosticDevice *device=&devices[device_count++];
    device->id=id;

    const char *name=SDL_GetJoystickNameForID(id);
    const char *path=SDL_GetJoystickPathForID(id);
    SDL_GUID guid=SDL_GetJoystickGUIDForID(id);
    char guid_text[64]="";
    SDL_GUIDToString(guid,guid_text,sizeof(guid_text));
    Uint16 vendor=SDL_GetJoystickVendorForID(id);
    Uint16 product=SDL_GetJoystickProductForID(id);
    bool mapped=SDL_IsGamepad(id);

    printf("DEVICE added id=%u name=\"%s\" gamepad=%s vendor=%04X product=%04X guid=%s path=%s\n",
        id,name?name:"?",yes_no(mapped),vendor,product,guid_text,
        path?path:"?");

    if(mapped) {
        device->gamepad=SDL_OpenGamepad(id);
        printf("  SDL_OpenGamepad: %s",
            device->gamepad?"success":"failed");
        if(device->gamepad)
            printf(" name=\"%s\"",SDL_GetGamepadName(device->gamepad));
        else printf(" error=\"%s\"",SDL_GetError());
        putchar('\n');
    }

    /* Open the joystick API too, to compare raw SDL events with gamepad events. */
    device->joystick=SDL_OpenJoystick(id);
    printf("  SDL_OpenJoystick: %s",
        device->joystick?"success":"failed");
    if(device->joystick)
        printf(" buttons=%d axes=%d hats=%d",
            SDL_GetNumJoystickButtons(device->joystick),
            SDL_GetNumJoystickAxes(device->joystick),
            SDL_GetNumJoystickHats(device->joystick));
    else printf(" error=\"%s\"",SDL_GetError());
    putchar('\n');
    fflush(stdout);
}

static void remove_device(SDL_JoystickID id) {
    DiagnosticDevice *device=find_device(id);
    if(!device)return;
    printf("DEVICE removed id=%u\n",id);
    if(device->gamepad)SDL_CloseGamepad(device->gamepad);
    if(device->joystick)SDL_CloseJoystick(device->joystick);
    int index=(int)(device-devices);
    devices[index]=devices[--device_count];
    memset(&devices[device_count],0,sizeof(devices[device_count]));
    fflush(stdout);
}

static const char *button_name(Uint8 button) {
    const char *name=SDL_GetGamepadStringForButton((SDL_GamepadButton)button);
    return name?name:"unknown";
}

static const char *axis_name(Uint8 axis) {
    const char *name=SDL_GetGamepadStringForAxis((SDL_GamepadAxis)axis);
    return name?name:"unknown";
}

static const char *hint_value(const char *name) {
    const char *value=SDL_GetHint(name);
    return value?value:"(default)";
}

static void print_event(const SDL_Event *event) {
    switch(event->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        inspect_device(event->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        remove_device(event->gdevice.which);
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
        inspect_device(event->jdevice.which);
        break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        remove_device(event->jdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        printf("GAMEPAD id=%u button=%u (%s) %s\n",
            event->gbutton.which,event->gbutton.button,
            button_name(event->gbutton.button),
            event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?"down":"up");
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        printf("GAMEPAD id=%u axis=%u (%s) value=%d\n",
            event->gaxis.which,event->gaxis.axis,
            axis_name(event->gaxis.axis),event->gaxis.value);
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        printf("JOYSTICK id=%u button=%u %s\n",
            event->jbutton.which,event->jbutton.button,
            event->type==SDL_EVENT_JOYSTICK_BUTTON_DOWN?"down":"up");
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        printf("JOYSTICK id=%u axis=%u value=%d\n",
            event->jaxis.which,event->jaxis.axis,event->jaxis.value);
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        printf("JOYSTICK id=%u hat=%u value=0x%02X\n",
            event->jhat.which,event->jhat.hat,event->jhat.value);
        break;
    default:
        break;
    }
    fflush(stdout);
}

int main(int argc,char **argv) {
    printf("SDL %u.%u.%u platform=%s\n",
        SDL_GetVersion()/1000000,(SDL_GetVersion()/1000)%1000,
        SDL_GetVersion()%1000,SDL_GetPlatform());
    printf("hints: HIDAPI=%s XBOX=%s XBOX_360=%s XBOX_ONE=%s MFI=%s IOKIT=%s\n",
        hint_value(SDL_HINT_JOYSTICK_HIDAPI),
        hint_value(SDL_HINT_JOYSTICK_HIDAPI_XBOX),
        hint_value(SDL_HINT_JOYSTICK_HIDAPI_XBOX_360),
        hint_value(SDL_HINT_JOYSTICK_HIDAPI_XBOX_ONE),
        hint_value(SDL_HINT_JOYSTICK_MFI),
        hint_value(SDL_HINT_JOYSTICK_IOKIT));

    if(!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());
        return 1;
    }

    int count=0;
    SDL_JoystickID *ids=SDL_GetJoysticks(&count);
    printf("SDL_GetJoysticks: %d device(s)\n",count);
    for(int i=0;ids && i<count;++i)inspect_device(ids[i]);
    SDL_free(ids);
    fflush(stdout);

    if(argc>1 && strcmp(argv[1],"--scan-only")==0) {
        SDL_Quit();
        return 0;
    }

    puts("Listening for controller input. Press buttons, move sticks/triggers; Ctrl-C exits.");
    for(;;) {
        SDL_Event event;
        while(SDL_PollEvent(&event))print_event(&event);
        SDL_Delay(5);
    }
}
