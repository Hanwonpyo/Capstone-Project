int analogPin=0;
int led=13;
int val=0;

void setup()
{
  pinMode(led,OUTPUT);
  Serial.begin(9600);
}

void loop()
{
  val=analogRead(analogPin);
  if(val>100)
  {
    digitalWrite(led,HIGH);
  }
  else
  {
    digitalWrite(led,LOW);
  }
  Serial.println(val);
  delay(500);
}

