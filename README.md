Roundhouse
==========

This is an SMTP multiplexer, which takes the input from an SMTP client connection and copies it to one or more SMTP servers. Intended as means to debug and test different mail server configurations using a production mail server's live data stream.  Requires LibSnert.


Usage
-----

```
usage: roundhouse [-Adqv][-i ip,...][-t timeout][-u name][-g name]
       [-c ca_pem][-C ca_dir][-k key_crt_pem][-K key_pass]
       [-w add|remove] server ...

-A              all down stream servers must connect, else 421 the client.
-c ca_pem       Certificate Authority root certificate chain file
-C ca_dir       Certificate Authority root certificate directory
-d              disable daemon mode and run as a foreground application
-g name         run as this group
-i ip,...       comma separated list of IPv4 or IPv6 addresses and
                optional :port number to listen on for SMTP connections;
                default is "[::0]:25,0.0.0.0:25"
-k key_crt_pem  private key and certificate chain file.  When left unset
                or explicitly set to an empty string then disable STARTTLS.
-K key_pass     password for private key; default no password
-q              x1 slow quit, x2 quit now, x3 restart, x4 restart-if
-t timeout      client socket timeout in seconds; default 300
-u name         run as this user
-v              x1 log SMTP; x2 SMTP and message headers; x3 everything
-w add|remove   add or remove Windows service; ignored on unix

server          host[:port] of down stream mail server to forward mail to;
                default port 25

roundhouse 0.8.3 Copyright 2005, 2022 by Anthony Howe. All rights reserved.
```
