#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BUTTON_COUNT 4
uint8_t BUTTON_PINS[BUTTON_COUNT] = {0,1,2,3};

// RGB LED
#define LED_R 21
#define LED_G 22
#define LED_B 23

struct __attribute__((packed)) ButtonMessage {
  uint8_t button_id;
  uint8_t action;
  uint32_t counter;
};

struct __attribute__((packed)) AckMessage {
  uint32_t counter;
  bool success;
};

// --- RTC MEMORY ---
RTC_DATA_ATTR uint32_t counter = 0;
RTC_DATA_ATTR uint8_t lastChannel = 1;
RTC_DATA_ATTR uint8_t targetMAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
RTC_DATA_ATTR bool isNodeKnown = false;

volatile bool ackReceived = false;

// ------------------------------------------------
// RGB HELPER
// ------------------------------------------------
void setRGB(bool r, bool g, bool b){
  // Active LOW LEDs
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// ------------------------------------------------
// DETECT WHICH BUTTON
// ------------------------------------------------
uint8_t detectButton(){
  for(int i=0;i<BUTTON_COUNT;i++){
    if(digitalRead(BUTTON_PINS[i])==LOW){
      return i+1;
    }
  }
  return 0;
}

// ------------------------------------------------
// SLEEP
// ------------------------------------------------
void goToSleep(){

  // wait for release
  bool released=false;
  while(!released){
    released=true;
    for(int i=0;i<BUTTON_COUNT;i++){
      if(digitalRead(BUTTON_PINS[i])==LOW){
        released=false;
      }
    }
    delay(10);
  }

  delay(50);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  setRGB(0,0,0);

  delay(200);

  uint64_t mask=0;
  for(int i=0;i<BUTTON_COUNT;i++){
    mask |= (1ULL<<BUTTON_PINS[i]);
  }

  esp_deep_sleep_enable_gpio_wakeup(mask,ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}

// ------------------------------------------------
// RECEIVE CALLBACK
// ------------------------------------------------
void OnDataRecv(const esp_now_recv_info_t *info,const uint8_t *incomingData,int len){

  if(len==sizeof(AckMessage)){
    AckMessage *ack=(AckMessage*)incomingData;

    if(ack->counter==counter && ack->success){
      ackReceived=true;

      if(!isNodeKnown){
        memcpy(targetMAC,info->src_addr,6);
        isNodeKnown=true;
      }
    }
  }
}

// ------------------------------------------------
// MAIN SETUP
// ------------------------------------------------
void setup(){

  Serial.begin(115200);

  for(int i=0;i<BUTTON_COUNT;i++){
    pinMode(BUTTON_PINS[i],INPUT);
  }

  pinMode(LED_R,OUTPUT);
  pinMode(LED_G,OUTPUT);
  pinMode(LED_B,OUTPUT);

  setRGB(0,0,1); // blue boot indicator

  delay(100);

  esp_sleep_wakeup_cause_t wakeup_reason=esp_sleep_get_wakeup_cause();

  if(wakeup_reason!=ESP_SLEEP_WAKEUP_GPIO){
    Serial.println("Fresh power-on. Sleeping.");
    goToSleep();
    return;
  }

  uint8_t button_id=detectButton();

  if(button_id==0){
    goToSleep();
  }

  Serial.printf("Button %d pressed\n",button_id);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if(esp_now_init()!=ESP_OK){
    goToSleep();
  }

  esp_now_register_recv_cb(OnDataRecv);

  ButtonMessage msg;
  msg.button_id=button_id;
  msg.action=1;
  msg.counter=++counter;

  // FAST PATH
  if(isNodeKnown){

    Serial.printf("Targeting known node on channel %d\n",lastChannel);

    esp_wifi_set_channel(lastChannel,WIFI_SECOND_CHAN_NONE);

    esp_now_peer_info_t peerInfo={};
    memcpy(peerInfo.peer_addr,targetMAC,6);
    peerInfo.channel=lastChannel;
    peerInfo.encrypt=false;
    peerInfo.ifidx=WIFI_IF_STA;

    esp_now_add_peer(&peerInfo);

    esp_now_send(targetMAC,(uint8_t*)&msg,sizeof(msg));

    unsigned long startWait=millis();

    while(millis()-startWait<50){

      if(ackReceived){

        setRGB(0,1,0); // green success
        delay(50);

        Serial.println("Instant success");

        goToSleep();
      }

      delay(1);
    }

    esp_now_del_peer(targetMAC);

    isNodeKnown=false;

    Serial.println("Node failed, sweeping...");
  }

  // BROADCAST SWEEP
  setRGB(1,1,0); // yellow sweep

  uint8_t broadcastMAC[]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  esp_now_peer_info_t bcPeer={};
  memcpy(bcPeer.peer_addr,broadcastMAC,6);
  bcPeer.channel=0;
  bcPeer.encrypt=false;
  bcPeer.ifidx=WIFI_IF_STA;

  esp_now_add_peer(&bcPeer);

  for(uint8_t ch=1;ch<=13;ch++){

    esp_wifi_set_channel(ch,WIFI_SECOND_CHAN_NONE);

    esp_now_send(broadcastMAC,(uint8_t*)&msg,sizeof(msg));

    unsigned long startWait=millis();

    while(millis()-startWait<30){

      if(ackReceived){

        lastChannel=ch;

        setRGB(0,1,0); // green success
        delay(80);

        Serial.printf("Found node on channel %d\n",ch);

        goToSleep();
      }

      delay(1);
    }
  }

  setRGB(1,0,0); // red fail
  delay(200);

  Serial.println("No nodes found");

  goToSleep();
}

void loop(){}