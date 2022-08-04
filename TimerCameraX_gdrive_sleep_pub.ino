#include "Arduino.h"
#include "freertos/FreeRTOS.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Base64.h"

#include "esp_camera.h"

const char* ssid     = "SSID"; // Your SSID
const char* password = "PASSWORD"; // Your password
const char* myDomain = "script.google.com";
String myScript = "/macros/s/XXXXXXXXXXXXXXXXXXXXXX/exec"; // Your GAS url
String myFilename = "filename=M5Camera.jpg"; // any
String mimeType = "&mimetype=image/jpeg";
String myImage = "&data=";

int waitingTime = 30000; //Wait 30 seconds to google response.

#include "camera_pins.h"
#include "bmm8563.h"
#include "battery.h"

// Time
char ntpServer[] = "ntp.jst.mfeed.ad.jp";
const long gmtOffset_sec = 9 * 3600;
const int  daylightOffset_sec = 0;
struct tm timeinfo;
RTC_DATA_ATTR time_t t = 0; // RTCスローメモリに変数を確保

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Init
  //bat_init();
  bmm8563_init();
  //led_init(CAMERA_LED_GPIO);

  // x sec later will wake up
  bmm8563_setTimerIRQ((5*60-30));

  // Test LED
  //led_brightness(1023);
  //delay(1000);

  // Halt
  bat_disable_output();
  
  Serial.begin(115200);
  delay(100);
  
  WiFi.mode(WIFI_STA);

  Serial.println("");
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);  

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("STAIP address: ");
  Serial.println(WiFi.localIP());
    
  Serial.println("");

  // ntp sync.
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while (!getLocalTime(&timeinfo)) {
    delay(1000);
  }
  //time(&t);
  t = mktime(&timeinfo) - (5*60 - 10);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA;  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA https://ja.wikipedia.org/wiki/%E7%94%BB%E9%9D%A2%E8%A7%A3%E5%83%8F%E5%BA%A6#%E8%A6%8F%E6%A0%BC
  config.jpeg_quality = 10;
  config.fb_count = 2;
  
  esp_err_t err = esp_camera_init(&config);
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  
  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  s->set_vflip(s, 1);//flip it back
  s->set_brightness(s, 1);//up the blightness just a bit
  s->set_saturation(s, -2);//lower the saturation

  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_SVGA);
  
  // rtc_date_t date;
  // date.year = 2020;
  // date.month = 9;
  // date.day = 20;
  // date.hour = 15;
  // date.minute = 59;
  // date.second = 06;
  // bmm8563_setTime(&date);
}

void loop() {
  // 内部時計の現在時刻の取得
  //rtc_date_t date;
  //bmm8563_getTime(&date);
  //Serial.printf("Time: %d/%d/%d %02d:%02d:%-2d\r\n", date.year, date.month, date.day, date.hour, date.minute, date.second);
  //Serial.printf("volt: %d mv\r\n", bat_get_voltage());
  getLocalTime(&timeinfo);

  // 前回処理からの経過時間を計算
  double dt = difftime(mktime(&timeinfo), t); // [s]
  //Serial.print(mktime(&timeinfo)); 
  //Serial.print(" - "); 
  //Serial.print(t); 
  //Serial.print(" = "); 
  //Serial.println(dt);

  // 所定時間経過していれば
  if (dt > 5*60)
  {
    t = mktime(&timeinfo);
    
    saveCapturedImage();
    //delay(5*60*1000);
    
    Serial.println("Wi-Fi disconnect...");
    WiFi.disconnect(true);
    
    Serial.println("Start deep sleep...");
    esp_deep_sleep((5*60-30)*1000000); // us
    //esp_sleep_enable_timer_wakeup(5*60*1000000); // us
    //esp_deep_sleep_start();
  }
  delay(100);
}

void saveCapturedImage() {
  Serial.println("Connect to " + String(myDomain));
  WiFiClientSecure client;
  
  client.setInsecure();
  if (client.connect(myDomain, 443)) {
    Serial.println("Connection successful");
    
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();  
    if(!fb) {
      Serial.println("Camera capture failed");
      delay(1000);
      ESP.restart();
      return;
    }
  
    char *input = (char *)fb->buf;
    char output[base64_enc_len(3)];
    String imageFile = "";
    for (int i=0;i<fb->len;i++) {
      base64_encode(output, (input++), 3);
      if (i%3==0) imageFile += urlencode(String(output));
    }
    String Data = myFilename+mimeType+myImage;
    
    esp_camera_fb_return(fb);
    
    Serial.println("Send a captured image to Google Drive.");
    
    client.println("POST " + myScript + " HTTP/1.1");
    client.println("Host: " + String(myDomain));
    client.println("Content-Length: " + String(Data.length()+imageFile.length()));
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println();
    
    client.print(Data);
    int Index;
    for (Index = 0; Index < imageFile.length(); Index = Index+1000) {
      client.print(imageFile.substring(Index, Index+1000));
    }
    
    Serial.println("Waiting for response.");
    long int StartTime=millis();
    while (!client.available()) {
      Serial.print(".");
      delay(100);
      if ((StartTime+waitingTime) < millis()) {
        Serial.println();
        Serial.println("No response.");
        //If you have no response, maybe need a greater value of waitingTime
        break;
      }
    }
    Serial.println();   
    while (client.available()) {
      Serial.print(char(client.read()));
    }  
  } else {         
    Serial.println("Connected to " + String(myDomain) + " failed.");
  }
  client.stop();
}

//https://github.com/zenmanenergy/ESP8266-Arduino-Examples/
String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
}
