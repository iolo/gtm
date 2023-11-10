#include <SPI.h>
#include <Ethernet.h>
#include <Dns.h>
#include <EEPROM.h>

#define DEBUG(s) Serial.print(s)
#define DEBUGLN(s) Serial.println(s)
#define DEBUG_HEX(n) Serial.print(n,HEX)
#define DEBUGLN_HEX(n) Serial.println(n,HEX)

void DEBUGLN_CHAR(char c) {
  if (c > ' ' && c < 0x80) {
    DEBUGLN((char)c);
  } else {
    DEBUG("0x");
    DEBUGLN_HEX(c);
  }
}       

/** 
 * EEPROM map
 * 0 : NET type('D', 'S')
 * 1 : inverse NET type
 * 2-5 : Static IP
 * 6 : Subnet Mask
 * 7~10 : Gateway
 * 11~14 : DNS Server
 */

// Enter a MAC address and IP address for your controller below.
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

EthernetClient client;
DNSClient dns;
int link_on = 0;
int telnet_on = 0;
char cmd_buf[80];
int cmd_pos = 0;

void reload() {
  DEBUGLN("reload");
  byte v1 = EEPROM.read(0);
  byte v2 = EEPROM.read(1);
DEBUG("EEPROM v1=");DEBUG_HEX(v1);DEBUG(",v2=");DEBUG_HEX(v2);DEBUGLN();
  if ((v1 == 'S') && ((v1 ^ v2) == 0xff))
  {
    // STATIC IP
    int i;
    byte d[15];
    for (i = 2; i < 15; i++)
    {
      d[i] = EEPROM.read(i);
    }

    IPAddress ip(d[2], d[3], d[4], d[5]);
    IPAddress gateway(d[7], d[8], d[9], d[10]);
    IPAddress dns(d[11], d[12], d[13], d[14]);

    uint32_t s = 0;
    for (i = 1; i <= 24; i++)
    {
      if (d[6] <= i)
        s |= 1;
      s <<= 1;
    }
    IPAddress subnet((s >> 24) && 0xff, (s >> 16) & 0xff, (s >> 8) & 0xff, s & 0xff);
    
    DEBUGLN("NOTICE: Initialize Ethernet with Static");
    Serial1.println("NOTICE: Initialize Ethernet with Static");
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    link_on = 0;
  }
  else
  {
    // DHCP
    DEBUGLN("NOTICE: Initialize Ethernet with DHCP");
    Serial1.println("NOTICE: Initialize Ethernet with DHCP");
    link_on = Ethernet.begin(mac);
  }

  if (link_on == 0) {
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      DEBUGLN("ERROR: ethernet module was not found.");
      Serial1.println("ERROR: ethernet module was not found.");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      DEBUGLN("ERROR: ethernet cable is not connected.");
      Serial1.println("ERROR: ethernet cable is not connected.");
    } else {
      link_on = 1;
    }
  }
}

void connect(const char *address, int port) {
  IPAddress server;

  dns.begin(Ethernet.dnsServerIP());
  if(dns.getHostByName(address, server) == 1) {
    DEBUG("address:");DEBUG(address);DEBUG("=");DEBUGLN(server);
    Serial1.print(address);
    Serial1.print(" = ");
    Serial1.println(server);
  }
  else {
    DEBUGLN("ERROR: DNS lookup failed");
    Serial1.println("ERROR: DNS lookup failed");
    return;
  }

  // if you get a connection, report back via serial:
  if (client.connect(server, port)) {
    DEBUGLN("Telnet connected");
    Serial1.println("Telnet connected");
    telnet_on = 1;
  } else {
    // if you didn't get a connection to the server:
    DEBUGLN("ERROR: connection failed");
    Serial1.println("ERROR: connection failed");
  }
}

void set_address(int position, const char *address)
{
  int subnet = -1;
  if (position == 2)
  {
    char *cut = address;
    while (*cut && (*cut != '/'))
    {
      cut++;
    }

    if (*cut)
    {
      *cut = '\0';
      subnet = atoi(cut + 1);
    }

    if ((subnet < 1) || (subnet > 23))
    {
      DEBUGLN("ERROR: invalid subnet mask");
      Serial1.println("ERROR: invalid subnet mask");
      return;
    }
  }

  IPAddress ip;
  if (dns.inet_aton(address, ip) == 0)
  {
    DEBUGLN("ERROR: invalid address");
    Serial1.println("ERROR: invalid address");
    return;
  }

  int i;
  for (i = 0; i < 4; i++)
  {
    EEPROM.update(position + i, ip[i]);
  }

  if (position == 2)
  {
    EEPROM.update(position + 4, subnet);
  }
}

void setup() {
  Serial.begin(9600);
  // Open serial communications and wait for port to open:
  Serial1.begin(4800, SERIAL_8N1); // XXX: apple basic is too slow to handle 9600
  // You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(10);  // Most Arduino shields
  reload();
  DEBUGLN("hello!");
}

bool first_run = false;
void loop() {
  if (first_run) {
    DEBUGLN("telnet example.com 80");
    connect("example.com", 80);
    delay(100);
    if (client.connected()) {
      DEBUGLN("GET / HTTP/1.0");
      client.println("GET / HTTP/1.0");
      client.println();
    }
    first_run = false;
  }
  // TEST!  arduino -> serial
  while (Serial.available() > 0) {
    char c = Serial.read();
    DEBUG("arduino->serial:");DEBUGLN_CHAR(c);
    Serial1.write(c);
  }

  //DEBUG(".");
  if (telnet_on)
  {
    // server to serial
    while (client.available()) {
      char c = client.read();
      //if (c > 0x7f) {
      //  c &= 0x7f;
      //}
      DEBUG("server->serial:");DEBUGLN_CHAR(c);
      Serial1.print(c);
    }

    // serial to server
    while (Serial1.available() > 0) {
      char inChar = Serial1.read();
      //if (inChar > 0x7f) {
      //  inChar &= 0x7f;
      //}
      if (client.connected()) {
        DEBUG("serial->server:");DEBUGLN_CHAR(inChar);
        client.print(inChar);
      }
    }
  
    // if the server's disconnected, stop the client:
    if (!client.connected()) {
      DEBUGLN();
      Serial1.println();
      DEBUGLN("disconnecting.");
      Serial1.println("disconnecting.");
      client.stop();
      telnet_on = 0;
    }
  }
  else
  {
    while (Serial1.available() > 0) {
      int inChar = Serial1.read();
      //if (inChar > 0x7f) {
      //  inChar &= 0x7f;
      //}
      DEBUG("serial->arduino:"); DEBUGLN_CHAR(inChar);
      if ((inChar == '\r') || (inChar == '\n'))
      {
        if (cmd_pos >= 80)
        {
          DEBUGLN("ERROR: command is too long");
          Serial1.println("ERROR: command is too long");
          cmd_pos = 0;
          return;
        }
        else if (cmd_pos == 0)
        {
          return;
        }

        cmd_buf[cmd_pos] = '\0';
        cmd_pos = 0;
        DEBUG("serial->arduino: cmd="); DEBUGLN(cmd_buf);

        if (strcmp(cmd_buf, "GTM") == 0)
        {
          DEBUGLN("NOTICE: ready");
          Serial1.println("NOTICE: ready");
          return;
        }
        else if (strncmp(cmd_buf, "GTM CONN ", 9) == 0)
        {
          char *cut = cmd_buf + 9;
          while (*cut && (*cut != ' '))
          {
            cut++;
          }

          int port = 80;
          if (*cut)
          {
            int port = atoi(cut + 1);
            *cut = '\0';
          }

          if ((port <= 0) || (port > 65535))
          {
            DEBUGLN("ERROR: invalid port number");
            Serial1.println("ERROR: invalid port number");
            return;
          }

          connect(cmd_buf + 9, port);
        }
        else if (strcmp(cmd_buf, "GTM SET NET RELOAD") == 0)
        {
          reload();
        }
        else if ((strcmp(cmd_buf, "GTM SET NET DHCP") == 0) ||
          (strcmp(cmd_buf, "GTM SET NET STATIC") == 0))
        {
          char v1 = cmd_buf[12];
          char v2 = ~v1;
          EEPROM.update(0, v1);
          EEPROM.update(1, v2);
          DEBUGLN("NOTICE: net set");
          Serial1.println("NOTICE: net set");
        }
        else if (strncmp(cmd_buf, "GTM SET IP ", 11) == 0)
        {
          set_address(2, cmd_buf + 11);
          DEBUGLN("NOTICE: ip set");
          Serial1.println("NOTICE: ip set");
        }
        else if (strncmp(cmd_buf, "GTM SET GW ", 11) == 0)
        {
          set_address(7, cmd_buf + 11);
          DEBUGLN("NOTICE: gw set");
          Serial1.println("NOTICE: gw set");
        }
        else if (strncmp(cmd_buf, "GTM SET DNS ", 12) == 0)
        {
          set_address(11, cmd_buf + 12);
          DEBUGLN("NOTICE: dns set");
          Serial1.println("NOTICE: dns set");
        }
        else
        {
          DEBUGLN("ERROR: command is invalid");
          Serial1.println("ERROR: command is invalid");
          return;
        }
      }
      else if (cmd_pos < 80)
      {
        cmd_buf[cmd_pos] = inChar;
        cmd_pos++;
      }
    }
  }
}