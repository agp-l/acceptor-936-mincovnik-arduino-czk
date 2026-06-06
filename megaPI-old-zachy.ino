#include <MeMegaPi.h>

// --- NASTAVITELNÉ PARAMETRY ---
int pozadovanaCena = 20;       // Cena za jedno otočení (zboží)
long stupneOtoceni = -360;      // O kolik stupňů se má motor otočit
int rychlostMotoru = 1000;     // Rychlost (kroky/s)
int mikroKroky = 16;           // Nastavení driveru (MegaPi má standardně 1/16)
// ------------------------------

// Inicializace displeje na Portu 7 a OnBoard driveru v Portu 1
Me7SegmentDisplay displej(PORT_7);
MeStepperOnBoard motor(1); // Číslo 1 odpovídá Portu 1 na MegaPi

const int pinDetekceMince = 2; 

volatile long celkovyPocetPreruseni = 0; 
volatile unsigned long casPoslednihoPreruseni = 0; 
unsigned long soucasnyZustatek = 0; 

void setup() {
  Serial.begin(9600);
  
  // Konfigurace pinu pro mincovník
  pinMode(pinDetekceMince, INPUT); 
  attachInterrupt(digitalPinToInterrupt(pinDetekceMince), preruseniPriVhozuMince, RISING); 
  
  // Konfigurace motoru pro MegaPi slot
  motor.setMicroStep(mikroKroky);
  motor.enableOutputs(); // Aktivuje napájení motoru
  motor.setMaxSpeed(rychlostMotoru);
  motor.setAcceleration(2000);
  
  // Výchozí stav displeje a konzole
  displej.display((int16_t)0);
  Serial.println("Automat pripraven k provozu.");
}

void loop() {
  // 1. KONTROLA MINCOVNÍKU
  noInterrupts();
  unsigned long posledniCas = casPoslednihoPreruseni;
  long pulzy = celkovyPocetPreruseni;
  interrupts();

  // Pokud mince doletěla (pauza > 200ms od posledního pulzu)
  if (pulzy > 0 && (millis() - posledniCas > 200)) {
    noInterrupts();
    celkovyPocetPreruseni = 0; 
    interrupts();

    soucasnyZustatek += pulzy;
    displej.display((int16_t)soucasnyZustatek);
    Serial.print("Vlozeno: ");
    Serial.print(pulzy);
    Serial.print(" Kc | Aktualni zustatek: ");
    Serial.println(soucasnyZustatek);
  }

  // 2. LOGIKA VÝDEJE (Otočení motoru)
  if (soucasnyZustatek >= (unsigned long)pozadovanaCena) {
    Serial.println("Dostatecny zustatek. Vydavam...");

    // Výpočet kroků: (stupně * (200 kroků motoru * mikrokrokování)) / 360 stupňů
    long krokyKVykonani = (stupneOtoceni * 200L * mikroKroky) / 360L;
    
    // Nastavení pohybu
    motor.move(krokyKVykonani);
    
    // Blokující smyčka: motor se točí, dokud nedojede do cíle
    while (motor.distanceToGo() != 0) {
      motor.run();
    }
    
    // Odečtení ceny (zbytek zůstává v paměti)
    soucasnyZustatek -= pozadovanaCena;
    
    // Aktualizace displeje po výdeji
    displej.display((int16_t)soucasnyZustatek);
    
    Serial.print("Vydano. Na uctu zustava: ");
    Serial.println(soucasnyZustatek);
  }
  
  // Nezbytné pro správnou funkci knihovny
  motor.run();
}

// Funkce přerušení - běží na pozadí
void preruseniPriVhozuMince() {
  celkovyPocetPreruseni++; 
  casPoslednihoPreruseni = millis(); 
}
