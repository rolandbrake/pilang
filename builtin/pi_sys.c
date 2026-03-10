#include <math.h>
#include <string.h>
#include "pi_sys.h"
#include "../pi_value.h"
#include "../list.h"

Value pi_fps(vm_t *vm, int argc, Value *argv)
{
    int fps = round(vm->fps);
    return NEW_NUM(fps);
}

Value _pi_type(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[type] expects at least one argument.");

    char *type = type_name(argv[0]);

    return NEW_OBJ(new_pistring(strdup(type)));
}

Value pi_error(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[error] expects at least one argument.");

    const char *str = as_string(argv[0]);
    printf("Error: %s\n", str);
    free((void *)str);
    return NEW_NIL();
}



Value pi_zen(vm_t *vm, int argc, Value *argv)
{

    return NEW_OBJ(new_pistring(strdup(

        "*********************************************\n"
        " ____ ___ ____   ____ ____  ___ ____ _____  \n"
        "|  _ \\_ _/ ___| / ___|  _ \\|_ _|  _ \\_   _|\n"
        "| |_) | |\\___ \\| |   | |_) || || |_) || |  \n"
        "|  __/| | ___) | |___|  _ < | ||  __/ | |  \n"
        "|_|  |___|____/ \\____|_| \\_\\___|_|    |_|  \n"
        "*********************************************\n"

        "\n"
        " The Zen of PiScript\n"
        " --------------------\n"
        " 1. Simplicity is power.\n"
        " 2. Functions shape the flow.\n"
        " 3. Tables hold the world.\n"
        " 4. Graphics tell the story.\n"
        " 5. 128 by 128, a universe unfolds.\n"
        " 6. Freedom in code, structure in choice.\n"
        " 7. Dynamic, yet precise.\n"
        " 8. Expressive, yet concise.\n"
        " 9. Less syntax, more meaning.\n"
        "10. A script should feel like art.\n"
        "\n"
        "PiScript is a canvas—paint with logic.\n"
        "----------------------------------------\n")));
}
