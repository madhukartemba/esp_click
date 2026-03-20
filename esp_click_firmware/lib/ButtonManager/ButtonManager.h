

enum ButtonType
{
    INPUT,
    INPUT_PULLUP,
    INPUT_PULLDOWN,
};

class ButtonManager
{
private:
    int *buttonPins;
    ButtonType buttonType;
};