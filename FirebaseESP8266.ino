#include <TimeLib.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>


#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <FirebaseArduino.h>

/*
   librerias para el RFID
*/

#define SS_PIN 4  //D2
#define RST_PIN 5 //D1
#include <MFRC522.h>

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.
int statuss = 0;
int out = 0;



// Set these to run example.
#define FIREBASE_HOST "yourproject.firebaseio.com" //host de la base de datos
#define FIREBASE_AUTH "yourtoken" //token de acceso
#define WIFI_SSID "----- you ssid wifi" //nombre de mi red wifi
#define WIFI_PASSWORD "yout password" //password de mi red wifi

// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org"; //servidor para ir a traer la hora
///HORA y fecha
const int timeZone = -6;     // Hora Guatemala
String fecha;
String horas;

WiFiUDP Udp;
unsigned int localPort = 8888;  // Puerto para comunicacion

time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);


void setup() {
  Serial.begin(9600);
  SPI.begin();      // Initiate  SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522

  // connect to wifi.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

  Udp.begin(localPort);
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH); //inicializo firebase
}


time_t prevDisplay = 0; // when the digital clock was displayed
void loop() {

  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();//tod la logica que yo quiera hacer se realizarà a partir de aca
      String dia(day());
      String mes(month());
      String anio(year());

      String hora(hour());
      String minuto(minute());
      String segundo(second());

      fecha = dia + "-" + mes + "-" + anio;
      horas = hora + ":" + minuto + ":" + segundo;

      //cuando se registra una nueva tarjeta
      if ( ! mfrc522.PICC_IsNewCardPresent())
      {
        return;
      }

      // Selecciona una tarjeta
      if ( ! mfrc522.PICC_ReadCardSerial())
      {
        return;
      }

      Serial.println();
      Serial.print(" UID tag :");
      String content = "";
      byte letter;

      for (byte i = 0; i < mfrc522.uid.size; i++) //para extraer el codigo de la tarjeta
      {
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));
      }

      content.toUpperCase(); //paso todo a mayusculas

      Serial.println();
      String urlname = "empleado/" + content.substring(1) + "/nombre";
      Serial.println(urlname);
      String nombre = Firebase.getString("empleado/" + content.substring(1) + "/nombre"); //realizamos la busqueda en la base de datos
      Serial.println("nombre es" + nombre );
      if (Firebase.failed()) {
        Serial.print("Error en users/____/apellido");
        Serial.print("Apellido: " + nombre);
        Serial.println(Firebase.error());
        return;
      }
      //llamada el metodo que registra
      Registro(nombre, content.substring(1), fecha);
    }
  }
}

void Registro(String nombre, String content, String date) {
  if (nombre == "") { // si nombre esta vacio es por que el codigo no está registrado en la base de datos
    Serial.print("Intento de acceso\n");
    StaticJsonBuffer<200> jsonBuffer; //Declaro el objeto a enviar
    JsonObject& regist = jsonBuffer.createObject();
    regist["codigo"] = content; //tarjeta que está intentando entrar
    regist["horaIntento"] = horas;

    String url = "failedaccess/" + fecha; //la rama a donde quiero enviar la data
    Firebase.push(url, regist); //envio un objeto a la base de datos
    if (Firebase.failed()) { //controlador de errores
      Serial.print("Error al enviar data de acceso fallido");
      Serial.println(Firebase.error());
      return;
    }

    return;
  }
  delay(300);

  String pendientes = Firebase.getString("registro/" + fecha + "/" + content + "/pendientes"); //pendiente contiene dos valores S=si o N =no

  if (Firebase.failed()) {
    Serial.print("Error ocurrio");
    Serial.print(Firebase.error());
    return;
  }
  if (pendientes == "N") { //Cuando pendientes sea N ya no se permitiran intentos de accesos
    Serial.println("Ya se han registrado su horarios de este día");
    return;
  }
  delay(300);

  if (pendientes == "S" || pendientes == "") { // si pendientes està en S o "" es porque si està pendiente el ingreso
    bool estado = Firebase.getBool("empleado/" + content + "/estado"); //Esto es para ir a ver si se guardarà en entrada o salida ya que si està vacio se ingresara y si està en S se modificaà su valor

    if (Firebase.failed()) {
      Serial.print("Error ocurrio");
      Serial.print(Firebase.error());
      return;
    }
    delay(300);

    if (estado == false) { // si es falso es porque es un nuevo dìa  y no se ha ingresado su tarjeta entonces lo registra
      StaticJsonBuffer<200> jsonBuffer;
      JsonObject& regist = jsonBuffer.createObject();
      regist["entrada"] = horas;
      regist["salida"] = horas;
      regist["pendientes"] = "S";

      regist.printTo(Serial);
      String url = "registro/" + fecha + "/" + content;
      Firebase.set(url, regist);

      if (Firebase.failed()) {
        Serial.print("pushing /logs failed:");
        Serial.println(Firebase.error());
        return;
      }
      delay(300);

      Firebase.setBool("empleado/" + content + "/estado", true); //coloca el estado en true para que diga que el ingreso ya se hizo
      if (Firebase.failed()) {
        Serial.print("error al setear en empleado");
        Serial.println(Firebase.error());
        return;
      }
      delay(300);
    } else {
      Firebase.setString("registro/" + fecha + "/" + content + "/salida", horas);  // en caso contrario solo modifica la hora para que se guarde como salida
      if (Firebase.failed()) {
        Serial.print("error en el ingreso de salida");
        Serial.println(Firebase.error());
        return;
      }
      delay(300);

      Firebase.setBool("empleado/" + content + "/estado", false); // y coloca el estado en falso en espera de un nuevo ingreso de tarjeta
      if (Firebase.failed()) {
        Serial.print("error al setear el estado en empleado");
        Serial.println(Firebase.error());
        return;
      }
      delay(300);

      Firebase.setString("registro/" + fecha + "/" + content + "/pendientes", "N"); // coloca en N para decir que no se realizo ninguna
      if (Firebase.failed()) {
        Serial.print("error al setear en pendientes en registro");
        Serial.println(Firebase.error());
        return;
      }
      delay(300);
    }
  }

}

/*Hora y fecha*/

void digitalClockDisplay()
{
  String dia(day());
  String mes(month());
  String anio(year());

  String hora(hour());
  String minuto(minute());
  String segundo(second());

  fecha = dia + "/" + mes + "/" + anio;
  horas = hora + ":" + minuto + ":" + segundo;
  //Serial.print(fecha);
  //Serial.print(" " + hora);
  //Serial.println();
}
/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
  IPAddress ntpServerIP; // NTP server's ip address
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// Enviando una peticion ntp al servidor
void sendNTPpacket(IPAddress & address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
