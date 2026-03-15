const int inputPins[] = {0,1,2,3,4,5};

const int redPin = 21;
const int greenPin = 22;
const int bluePin = 23;

const int adcPin = 6;

void setup() {

  Serial.begin(115200);

  for(int i=0;i<6;i++)
    pinMode(inputPins[i], INPUT);

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  // turn LED off initially
  digitalWrite(redPin, HIGH);
  digitalWrite(greenPin, HIGH);
  digitalWrite(bluePin, HIGH);
}

void setRGB(bool r,bool g,bool b)
{
  // invert for active-low LED
  digitalWrite(redPin, !r);
  digitalWrite(greenPin, !g);
  digitalWrite(bluePin, !b);
}

void loop() {

  Serial.print("GPIO: ");

  for(int i=0;i<6;i++)
  {
    int val = digitalRead(inputPins[i]);
    Serial.print("G");
    Serial.print(inputPins[i]);
    Serial.print(":");
    Serial.print(val);
    Serial.print(" ");
  }

  int adc = analogRead(adcPin);

  Serial.print("| ADC6: ");
  Serial.print(adc);
  Serial.println();

  bool g4 = digitalRead(4) == LOW;
  bool g5 = digitalRead(5) == LOW;

  if(g4 && g5)
    setRGB(0,1,0); // green
  else if(g4)
    setRGB(1,0,0); // red
  else if(g5)
    setRGB(0,0,1); // blue
  else
    setRGB(1,1,1); // white

  delay(200);
}