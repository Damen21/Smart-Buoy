
# IoT merilna boja za spremljanje kakovosti vode

Projekt predstavlja prototip merilne boje za spremljanje kakovosti vode. Sistem je sestavljen iz **boje** in **sprejemnika na obali**.

Boja uporablja mikrokrmilnik **Heltec WiFi LoRa 32 V4** ter meri:

* temperaturo vode z DS18B20,
* pH,
* motnost vode.

Analogna senzorja za pH in motnost sta povezana prek pretvornika **ADS1115**.

Po izvedeni meritvi boja podatke pošlje prek **LoRa povezave na 868 MHz**. Interval merjenja se lahko prilagaja glede na hitrost spremembe temperature vode, s čimer se zmanjša število nepotrebnih meritev in poraba energije.

Sprejemnik uporablja drug Heltec ESP32, ki:

* sprejema LoRa pakete,
* prebere podatke meritev,
* beleži RSSI in SNR povezave,
* se poveže z Wi-Fi omrežjem,
* podatke pošlje v **Firebase Realtime Database**.

Osnovni tok podatkov:

```text
Senzorji
   ↓
Merilna boja
   ↓
LoRa 868 MHz
   ↓
Sprejemnik
   ↓
Wi-Fi
   ↓
Firebase Realtime Database
```

Struktura projekta:

`FINAL_BOJA.ino` vsebuje program za izvajanje meritev in LoRa oddajanje, `Receiver_Final.ino` pa program za sprejem podatkov in njihovo shranjevanje v Firebase.

Projekt je bil izdelan v okviru diplomske naloge na **Fakulteti za računalništvo in informatiko Univerze v Ljubljani**.
