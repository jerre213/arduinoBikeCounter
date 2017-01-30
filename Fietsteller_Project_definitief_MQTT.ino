#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>

#include <Arduino.h>
#include <SPI.h>
#include <PubSubClient.h>

int connectorDpSensor = A0;
int rainSensor = A2;
// the current readout value from the DP sensor
float dpSensorValue = 0;

//the average value is used to filter as a reference value for when there is no pressure, it wil continually be readjusted 
//when there is no pressure and the dpSensorValue is semi-constant
int averageDpValue;
//the threshold is equal to the average value + 10%
//the threshold is used to evaluate that a higher dpSensor value is actually pressure and not a slight variation in the value
int dpThreshold = 0;
int valueThresholdAboveAverage = 15;
int upperLimit = 800;

//counter which counts the amount of times the DpSensorValue was higher then or equal to the previous 
int dpIsIncreasing = 0;
//aantal keer nodig om te checken dat dp terug aan het stijgen is en geen dipje omhoog geeft
#define maxAantalKeerIncreasing 1
//counter whicht counts the amount of times the DpSensorValue has gone down after going up
int dpIsDecreasing = 0;
//maxAantalkeerDecreasing is het maximum aantal keer dat een Dp waarde kleiner mag zijn dan de vorige waarde om zeker te zijn dat de piek bereikt is en dat het niet gwn een dipje in de meting is
#define maxAantalKeerDecreasing 3

//the highest value reached in a pressureWave
int dpPeak = 0;
int dpPeakWheel1 = 0;
int dpPeakWheel2 = 0;
unsigned long peakWheel1Time = 0;
unsigned long peakWheel2Time = 0;
unsigned long timeBetweenWheels = 0;

#define AMOUNTOFARRAYVALUES 50
int averagePressureArray[AMOUNTOFARRAYVALUES] = {0};
int arrayIndexCounter = 0;
int previousArrayValue = 0;
float arraySum = 0;

int loopsWithoutSecondWheel = 0;


//mqtt
IPAddress server(169, 254, 98, 2); // numeric IP for pc (no DNS)
#define MQTT_PORT 1883

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 169, 254, 98, 5 };
EthernetClient ethernetClient;
PubSubClient client(ethernetClient);
char message[50];


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);          //  setup serial
  pinMode(LED_BUILTIN, OUTPUT);

while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  //these values are hardcoded now but will eventually be dynamically readjusted
  int eersteMeting = analogRead(connectorDpSensor);
  averageDpValue = eersteMeting;
  dpThreshold = eersteMeting + valueThresholdAboveAverage;
  dpPeak = dpThreshold;
  
  Serial.println(analogRead(connectorDpSensor));
  //array initialiseren
  for(int i = 0; i < AMOUNTOFARRAYVALUES; i++){
      averagePressureArray[i] = eersteMeting;
  }
  arraySum = eersteMeting * AMOUNTOFARRAYVALUES;


 //mqtt
 Ethernet.begin(mac, ip);
  
  client.setServer(server, MQTT_PORT);
  client.setCallback(callback);
  client.subscribe("settings");
  Serial.print("connected");

  
  Serial.println("ready to go");
}

void loop() {
  dpSensorValue = analogRead(connectorDpSensor);
  //Serial.println(analogRead(rainSensor));
  //Serial.println(dpSensorValue);
  //dpPeak is initieel gelijk aan de DpThreshold
  //de piek zoeken
  if(dpSensorValue >= dpPeak){
    dpIsIncreasing += 1;
    //if the measured value is larger than maxAantalKeerIncreasing you are sure the value is really going up and it is nog an upwards dip
    if(dpIsIncreasing > maxAantalKeerIncreasing){
      dpPeak = dpSensorValue;
    }    
    //om te vermijden dat de dpIsDecreasing teller al te hoog wordt in het stijgende deel van de curve bij dipjes in de meetwaarde ga je die ontdoen bij een stijging
    if(dpIsDecreasing > 0){
      dpIsDecreasing -= 1;
    }
  }else if (dpSensorValue < dpPeak && dpSensorValue > dpThreshold){
    dpIsDecreasing += 1;
    dpIsIncreasing = 0;
    //peak = reached (you are shure the going down is not a dip)
    if(dpIsDecreasing > maxAantalKeerDecreasing){
       //piek is bereikt
       dpIsIncreasing = 0;
       if(dpPeakWheel1 == 0){
          dpPeakWheel1 = dpPeak;
          dpPeak = dpThreshold;
          peakWheel1Time = millis();
          dpIsDecreasing = 0;
          Serial.println("eerste wiel");
       }else if(dpPeakWheel1 != 0 && dpPeakWheel2 == 0){
          dpPeakWheel2 = dpPeak;
          dpPeak = dpThreshold;
          peakWheel2Time = millis();
          dpIsDecreasing = 0;
          Serial.println("tweede wiel");
       }else{
          //ERROR -> mag niet gebeuren
          Serial.println("error");
       }
       if(dpPeakWheel1 != 0 && dpPeakWheel2 != 0){
          digitalWrite(LED_BUILTIN, HIGH);
          //fiets is erover gereden
          timeBetweenWheels = peakWheel2Time - peakWheel1Time;
          
          Serial.println(timeBetweenWheels);
          Serial.println(dpPeakWheel1);
          Serial.println(dpPeakWheel2);
          Serial.println(dpThreshold);
          Serial.println(averageDpValue);
          Serial.println();

          if(!client.connected()){
            if(!client.connect("arduinoBoard")){
              Serial.println("connection failed");
            }
            //client.subscribe("settings");
          }
          if(client.connected()){
            String messageStr = String(timeBetweenWheels) + "-" + String(dpPeakWheel1) + "-" + String(dpPeakWheel2) + "-" + String(analogRead(rainSensor));
            messageStr.toCharArray(message, 50);
            if(!client.publish("bikeCounter/counter", message)){
              Serial.println("cannot publish value");
            }
          }       
          
          //reset values
          peakWheel1Time = 0;
          peakWheel2Time = 0;
          timeBetweenWheels = 0;
          dpPeakWheel1 = 0;
          dpPeakWheel2 = 0;
          dpPeak = dpThreshold;
          dpIsDecreasing = 0;
          digitalWrite(LED_BUILTIN, LOW);
       }
       
    }
  }else if (dpSensorValue <= dpThreshold){
    dpIsIncreasing = 0;
    //populate circular array to calculate average value when no pressure is present
    if(arrayIndexCounter >= AMOUNTOFARRAYVALUES){
      arrayIndexCounter = 0;
    }
    previousArrayValue = averagePressureArray[arrayIndexCounter];
    averagePressureArray[arrayIndexCounter++] = dpSensorValue;
    arraySum = arraySum - previousArrayValue + dpSensorValue;
    averageDpValue = (int)(arraySum / AMOUNTOFARRAYVALUES);
    dpThreshold = averageDpValue + valueThresholdAboveAverage;

    //when it takes too long for the second wheel to pass, there has probably been a mistake
    if(dpPeakWheel1 != 0 && dpPeakWheel2 == 0){
      loopsWithoutSecondWheel += 1;
      //depending on the amount of delay between loops
      //4 seconds
      if(loopsWithoutSecondWheel > 200){
        loopsWithoutSecondWheel = 0;
        dpPeakWheel1 = 0;
        Serial.println("second wheel took too long");
      }
    }  
  }
  delay(15);

}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println(topic);
  if(topic == "settings"){
    Serial.println("setting ontvangen");
    valueThresholdAboveAverage = (int)payload;
  }
}

