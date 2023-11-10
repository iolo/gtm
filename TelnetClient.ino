#include <SPI.h>
#include <Ethernet.h>
#include <Dns.h>
#include <EEPROM.h>

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
  byte v1 = EEPROM.read(0);
  byte v2 = EEPROM.read(1);

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
    
    Serial1.println("NOTICE: Initialize Ethernet with Static");
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    link_on = 0;
  }
  else
  {
    // DHCP
    Serial1.println("NOTICE: Initialize Ethernet with DHCP");
    link_on = Ethernet.begin(mac);
  }

  if (link_on == 0) {
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial1.println("ERROR: ethernet module was not found.");
    } else if (Ethernet.linkStatus() == LinkOFF) {
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
    Serial1.print(address);
    Serial1.print(" = ");
    Serial1.println(server);
  }
  else {
    Serial1.println("ERROR: DNS lookup failed");
    return;
  }

  // if you get a connection, report back via serial:
  if (client.connect(server, port)) {
    Serial1.println("Telnet connected");
    telnet_on = 1;
  } else {
    // if you didn't get a connection to the server:
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
      Serial1.println("ERROR: invalid subnet mask");
      return;
    }
  }

  IPAddress ip;
  if (dns.inet_aton(address, ip) == 0)
  {
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
  // Open serial communications and wait for port to open:
  Serial1.begin(9600);

  // You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(10);  // Most Arduino shields

  reload();
}

void loop() {
  if (telnet_on)
  {
    // server to serial
    if (client.available()) {
      char c = client.read();
      Serial1.print(c);
    }
  
    // serial to server
    while (Serial1.available() > 0) {
      char inChar = Serial1.read();
      if (client.connected()) {
        client.print(inChar);
      }
    }
  
    // if the server's disconnected, stop the client:
    if (!client.connected()) {
      Serial1.println();
      Serial1.println("disconnecting.");
      client.stop();
      telnet_on = 0;
    }
  }
  else
  {
    while (Serial1.available() > 0) {
      char inChar = Serial1.read();
      if ((inChar == '\r') || (inChar == '\n'))
      {
        if (cmd_pos >= 80)
        {
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

        if (strcmp(cmd_buf, "GTM") == 0)
        {
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
          char v1 = cmd_buf[12[;
          char v2 = ~v1;
          EEPROM.update(0, v1);
          EEPROM.update(1, v2);
          Serial1.println("NOTICE: net set");
        }
        else if (strncmp(cmd_buf, "GTM SET IP ", 11) == 0)
        {
          set_address(2, cmd_buf + 11);
          Serial1.println("NOTICE: ip set");
        }
        else if (strncmp(cmd_buf, "GTM SET GW ", 11) == 0)
        {
          set_address(7, cmd_buf + 11);
          Serial1.println("NOTICE: gw set");
        }
        else if (strncmp(cmd_buf, "GTM SET DNS ", 12) == 0)
        {
          set_address(11, cmd_buf + 12);
          Serial1.println("NOTICE: dns set");
        }
        else
        {
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
