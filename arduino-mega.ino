/* =========================================================================
   PRODEJNÍ AUTOMAT S MINCOVNÍKEM (Arduino Mega + TB6600 + TM1637)
   ========================================================================= */

// Vložení potřebných knihoven (musí být nainstalovány přes Správce knihoven)
#include <AccelStepper.h>
#include <TM1637Display.h>

// ==========================================
// 1. NASTAVENÍ PINŮ (Kde je co připojeno)
// ==========================================

// Mincovník musí být na pinu, který podporuje tzv. "Hardwarové přerušení".
// Na desce Arduino Mega jsou to piny 2, 3, 18, 19, 20, 21. Pin 2 je ideální.
const int pinDetekceMince = 2; 

// LED Displej TM1637 (Může být na jakýchkoliv digitálních pinech)
const int CLK_PIN = 4;
const int DIO_PIN = 5;

// Driver krokového motoru TB6600
const int STEP_PIN = 7; // Pin pro posílání pulzů (kroků)
const int DIR_PIN = 6;  // Pin pro určování směru (dopředu/dozadu)


// ==========================================
// 2. VYTVOŘENÍ OBJEKTŮ (Příprava hardwaru)
// ==========================================

// Vytvoříme objekt 'displej'.
TM1637Display displej(CLK_PIN, DIO_PIN);

// Vytvoříme objekt 'motor'.
// První parametr '1' (nebo AccelStepper::DRIVER) říká knihovně, že používáme 
// profesionální driver (jako je TB6600), který ke svému řízení potřebuje jen 2 piny (Step a Dir).
AccelStepper motor(1, STEP_PIN, DIR_PIN);


// ==========================================
// 3. PARAMETRY TVÉHO AUTOMATU (Zde si to nastav)
// ==========================================
int pozadovanaCena = 75;       // Kolik Kč musí uživatel vhodit
long stupneOtoceni = 360;      // O kolik stupňů se otočí mechanismus (1 otáčka = 360)
int rychlostMotoru = 1000;     // Rychlost otáčení (kroky za vteřinu)

// DŮLEŽITÉ: TB6600 má na sobě malé páčky (DIP switche), kterými se nastavuje mikrokrokování.
// Standardní motor má 200 kroků na otáčku. Pokud na TB6600 nastavíš páčky na "8 Microsteps",
// znamená to, že 1 otáčka = 200 * 8 = 1600 kroků. Zde doplň číslo podle toho, jak máš TB6600 nastaveno.
int krokyNaOtacku = 1600; 


// ==========================================
// 4. PROMĚNNÉ PRO POČÍTÁNÍ (Paměť automatu)
// ==========================================

// Slovo "volatile" je extrémně důležité! Říká procesoru Arduina: 
// "Pozor, tato proměnná se může změnit kdykoliv na pozadí, neukládej si ji do zkratek, 
// ale vždy si ji přečti přímo z hlavní paměti RAM."
volatile long celkovyPocetPreruseni = 0; 
volatile unsigned long casPoslednihoPreruseni = 0; 

// Hlavní pokladna - ukládá se zde částka, dokud není vydáno zboží
unsigned long soucasnyZustatek = 0; 


// ==========================================
// 5. FUNKCE SETUP (Proběhne jen jednou po startu)
// ==========================================
void setup() {
  Serial.begin(9600); // Zapne komunikaci s počítačem (pro hledání chyb)
  
  // -- PŘÍPRAVA MINCOVNÍKU --
  pinMode(pinDetekceMince, INPUT); 
  // Zapneme hlídání pinu 2 na pozadí. Jakmile na pinu stoupne napětí (RISING),
  // Arduino okamžitě přeruší cokoliv, co zrovna dělá, a spustí funkci "preruseniPriVhozuMince".
  attachInterrupt(digitalPinToInterrupt(pinDetekceMince), preruseniPriVhozuMince, RISING); 
  
  // -- PŘÍPRAVA DISPLEJE --
  displej.setBrightness(0x0f);     // Nastavení jasu: 0x00 je nejméně, 0x0f je maximum
  displej.showNumberDec(0, false); // Vykreslí úvodní nulu na displeji
  
  // -- PŘÍPRAVA MOTORU --
  motor.setMaxSpeed(rychlostMotoru); // Nastaví maximální rychlost
  motor.setAcceleration(500);        // Plynulý rozjezd (čím menší číslo, tím pomaleji se rozjíždí)
  
  Serial.println("Automat je pripraven a ceka na mince...");
}


// ==========================================
// 6. HLAVNÍ SMYČKA (Opakuje se neustále dokola)
// ==========================================
void loop() {
  
  // -----------------------------------------------------------------
  // KROK A: BEZPEČNÉ PŘEČTENÍ HODNOT Z MINCOVNÍKU
  // -----------------------------------------------------------------
  // Funkce noInterrupts() na zlomek milisekundy zakáže mincovník. 
  // Proč? Pokud bychom proměnnou právě četli a v tu samou mikrosekundu by 
  // propadla mince, data by se mohla poškodit (tzv. Race Condition).
  noInterrupts();
  unsigned long posledniCas = casPoslednihoPreruseni;
  long pulzy = celkovyPocetPreruseni;
  interrupts(); // Opět mincovník zapneme

  // -----------------------------------------------------------------
  // KROK B: ZPRACOVÁNÍ VHOZENÉ MINCE
  // -----------------------------------------------------------------
  // Zkontrolujeme: "Je tam nějaký pulz?" A ZÁROVEŇ "Uběhlo už 200 ms od posledního pulzu?"
  // Mincovník totiž pošle např. pět pulzů za sebou (5 Kč). My čekáme 200 ms, 
  // abychom měli jistotu, že série pulzů skončila a mince propadla celá.
  if (pulzy > 0 && (millis() - posledniCas > 200)) {
    
    // Nyní už víme, že mince propadla. Musíme hlavní čítač pulzů na pozadí vynulovat,
    // aby byl připraven na další minci. Zase ho při nulování pro jistotu zamkneme.
    noInterrupts();
    celkovyPocetPreruseni = 0; 
    interrupts();

    // Přičteme peníze do pokladny (současný zůstatek)
    soucasnyZustatek = soucasnyZustatek + pulzy;
    
    // Zobrazíme na LED displeji a pošleme info do počítače
    displej.showNumberDec(soucasnyZustatek, false); 
    Serial.print("Vlozeno. Aktualni zustatek: ");
    Serial.println(soucasnyZustatek);
  }

  // -----------------------------------------------------------------
  // KROK C: VÝDEJ ZBOŽÍ (Otočení motoru)
  // -----------------------------------------------------------------
  // Pokud je v pokladně víc (nebo stejně) peněz, než stojí zboží
  if (soucasnyZustatek >= (unsigned long)pozadovanaCena) {
    
    Serial.println("Dostatecny zustatek. Rozkrucim motor...");

    // VÝPOČET KROKŮ: Trocha matematiky.
    // (Počet stupňů * Kroky na otáčku) děleno 360 = počet kroků, které musíme poslat motoru.
    // Písmeno "L" za číslem (360L) znamená "Long". Nutí to Arduino počítat s velkými 
    // čísly, aby nedošlo k přetečení paměti kalkulačky.
    long krokyKVykonani = (stupneOtoceni * krokyNaOtacku) / 360L;
    
    // Řekneme motoru, o kolik kroků od aktuální pozice se má pohnout
    motor.move(krokyKVykonani);
    
    // BEZPEČNOSTNÍ BLOK: Dokud motor nedojede do cíle (vzdálenost k cíli není nula), 
    // toč jím. Funkce "while" zde zastaví zbytek programu. To je u automatu správně!
    // Díky tomu uživatel nemůže házet další mince, zatímco mechanika něco vydává.
    while (motor.distanceToGo() != 0) {
      motor.run(); // Příkaz, který udělá jeden fyzický minikrok motoru
    }
    
    // Motor dojel. Odečteme cenu zboží. 
    // Pokud vhodil 100 Kč a cena je 75, zůstane mu tu 25 Kč k dobru pro další nákup.
    soucasnyZustatek = soucasnyZustatek - pozadovanaCena;
    
    // Aktualizujeme displej
    displej.showNumberDec(soucasnyZustatek, false);
    
    Serial.print("Zbozi vydano. Zbytek kreditu: ");
    Serial.println(soucasnyZustatek);
  }
}

// ==========================================
// 7. FUNKCE PŘERUŠENÍ (Volá se sama na pozadí)
// ==========================================
// Tato funkce nesmí obsahovat příkazy delay(), Serial.print() ani manipulaci s displejem.
// Musí být bleskově rychlá - jen zaznamená, že pípla mince a poznamená si čas.
void preruseniPriVhozuMince() {
  celkovyPocetPreruseni = celkovyPocetPreruseni + 1; 
  casPoslednihoPreruseni = millis(); 
}
