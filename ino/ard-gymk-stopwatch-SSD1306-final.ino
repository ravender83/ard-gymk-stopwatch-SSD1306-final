/**
 * Name:     Arduino - test wyswietlacza oled 128x64
 * Autor:    Piotr Ślęzak
 * Web:      https://github.com/ravender83
 * Date:     2017/05/25
*/
#define version "1.5.10"

#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <Bounce2.h>
#include <eRCaGuy_Timer2_Counter.h>
#include <SimpleList.h>

#define DEBUG 1
#define WYPELNIJoff

#define I2C_ADDRESS 0x3C // adres I2C 

#define pin_sensor 2		// wejście czujnika laserowego
#define pin_ex_reset 4		// wejście przycisku reset nożnego
#define pin_reset 10		// wejście przycisku reset na urządzeniu
#define pin_menu  11		// wejście przycisku menu na urządzeniu
#define int_max_times_nr 6

#define pout_ready_led 9	// dioda led sygnalizująca o gotowości do pomiaru
#define pout_running_led 8	// dioda led sygnalizująca o trwaniu pomiaru
#define pout_buzzer	7		// buzzer informujący o przecięciu wiązki
#define czas_piszczenia_ms 1000 	// czas piszczenia w [ms]

#define debounce_time_ms 10
#define max_ekran 2 // liczba dostepnych ekranow
#define logo_time 100 // czas wyswietlania logo w ms
#define czas_nieczulosci_ms 1*1000000 // czas przed upływem którego nie da się zresetować pomiaru

#define pin_sensor_gp8 3 // pin czujnika licznika okrążeń w gp8
#define pin_gp8_mode 5 // przełącznik trybu GP8

int buzzer_time = 0;

char buf_akt_czas[10] = "00:00:000";
char buf_best_czas[10] = "00:00:000";
char bufms[4] = "000";
char bufsek[3] = "00";
char bufmin[3] = "00";
char buftemp[3] = "00";

SimpleList<long> lista_czasow;

SSD1306AsciiAvrI2c oled;

boolean state_reset = false;  // przycisk reset na urządzeniu
boolean state_ex_reset = false; // przycisk reset nożny
boolean state_menu = false; // przycisk menu na urządzeniu
boolean state_sensor = false; // czujnik laserowy
int ekran = 0; // 0 - logo, 1 - aktualny czas, 2 - ekran archiwalny

boolean working = false;
boolean finish = false;
boolean best = false;
boolean dopisano = false; // pomiar dopisano do listy

unsigned long czas_startu;
unsigned long czas_konca;
volatile unsigned long mils;
unsigned long czas_aktualny;

boolean buzzer_on = false; // załączenie buzzera
volatile boolean buzzer_switch_on = false;
unsigned long previousMillisBuzzer;
unsigned long currentMillisBuzzer;

boolean gp8_mode = false; 
int okrazenie = 4; // licznik okrazen 0
volatile boolean gp8_sensor_active = false; // zawodnik przeciął laser
boolean gp8_dopisano = false;
boolean last_state = false;
#define czas_nieczulosci_gp8_ms 2000 
#define czas_piszczenia_GP8_ms 2000
boolean gp8_buzzer = false;
unsigned long previousMillisGP8;
unsigned long currentMillisGP8;

Bounce pin_reset_deb = Bounce(); 
Bounce pin_ex_reset_deb = Bounce(); 
Bounce pin_menu_deb = Bounce(); 
Bounce pin_gp8_mode_deb = Bounce();

void setup()
{
	timer2.setup();

	// Ustawienie wejść, wyjść
	pinMode(pin_sensor, INPUT); // czujnik laserowy
	pinMode(pin_sensor_gp8, INPUT); // czujnik laserowy ilości okrążeń GP8

	pinMode(pin_gp8_mode, INPUT_PULLUP); // przełącznik trybu
	pin_gp8_mode_deb.attach(pin_gp8_mode);
	pin_gp8_mode_deb.interval(debounce_time_ms);
	
	pinMode(pin_reset, INPUT_PULLUP); // przycisk reset
	pin_reset_deb.attach(pin_reset);
	pin_reset_deb.interval(debounce_time_ms);

	pinMode(pin_ex_reset, INPUT_PULLUP); // przycisk nożny reset
	pin_ex_reset_deb.attach(pin_ex_reset);
	pin_ex_reset_deb.interval(debounce_time_ms);

	pinMode(pin_menu, INPUT_PULLUP); // przycisk menu
	pin_menu_deb.attach(pin_menu);
	pin_menu_deb.interval(debounce_time_ms);

	pinMode(pout_ready_led, OUTPUT);	// dioda zielona led
	digitalWrite(pout_ready_led, HIGH);

	pinMode(pout_running_led, OUTPUT);	// dioda czerwona led
	digitalWrite(pout_running_led, HIGH);

	pinMode(pout_buzzer, OUTPUT);		// buzzer
	digitalWrite(pout_buzzer, HIGH);

	oled.begin(&Adafruit128x64, I2C_ADDRESS);

	wyswietlEkran(0); // wyświetlenie logo
	delay(logo_time);
	oled.clear();
	ekran = 1; // aktualny czas

	lista_czasow.reserve(int_max_times_nr);
	lista_czasow.clear();

	attachInterrupt(digitalPinToInterrupt(pin_sensor), fsensor, FALLING);
	attachInterrupt(digitalPinToInterrupt(pin_sensor_gp8), fgp8sensor, FALLING);

	#ifdef WYPELNIJ
	// wypełnienie tablicy archiwalnej pomiarami	
	for (int i=1; i<=int_max_times_nr; i++) {
		sprintf(buftemp, "%d)",i);
		lista_czasow.push_front(buftemp);
	}
	#endif

	#ifdef DEBUG
	Serial.begin(9600);
	#endif
}

void loop()
{
	readInputs();

	// wciśnięto przycisk MENU
	if (state_menu == HIGH)
	{
		ekran++;
		if (ekran>max_ekran) {ekran = 1;}
		oled.clear();
		state_menu = LOW;
	}

	// wciśnięto przycisk RESETU
	if ((state_reset) || (state_ex_reset))
	{		
		if (state_ex_reset) ekran=1;
		working = false;
		finish = false;
		best = false;
		czas_startu = 0;
		czas_konca = 0;
		czas_aktualny = 0;
		okrazenie = 0;
		state_sensor = LOW;
		gp8_sensor_active = LOW;
		gp8_dopisano = LOW;
		gp8_buzzer = LOW;
		state_menu = LOW;
		state_reset = LOW;
		state_ex_reset = LOW;	
		dopisano = LOW;	
		buzzer_time = czas_piszczenia_ms;
		oled.clear();
	}

	// przecięto wiązkę zliczającą gp8
	if ((gp8_sensor_active == HIGH) && (gp8_dopisano == LOW)) {
		okrazenie++;
		gp8_dopisano = HIGH;
		previousMillisGP8 = millis();	
	}

	// jeśli liczba okrążeń jest 5, piśnij
	if ((okrazenie == 5) && (gp8_buzzer == LOW)) {
		gp8_buzzer = HIGH;
		buzzer_switch_on == HIGH;
		buzzer_time = czas_piszczenia_GP8_ms;
	}

	if (buzzer_on == HIGH) Serial.println("buzzer_on HIGH"); else  Serial.println("buzzer_on LOW");

	// konieczność uruchomienia buzzera
	if ((buzzer_switch_on == HIGH) && (buzzer_on == LOW)) {
		buzzer_switch_on = LOW;
		buzzer_on = HIGH;
		previousMillisBuzzer = millis();
	}

	// rozpoczęto pomiar
	if ((state_sensor == HIGH) && (working == LOW) && (finish == LOW))
	{
		czas_startu = mils;
		working = HIGH;
		state_sensor = LOW;		
	}

	// podczas pomiaru
	if ((state_sensor == LOW) && (working == HIGH) && (finish == LOW))
	{
		czas_aktualny = (timer2.get_count() - czas_startu);
	}

	// zakończono pomiar
	if ((state_sensor == HIGH) && (working == HIGH) && (finish == LOW))
	{
		czas_konca = mils;
		working = LOW;
		finish = HIGH;
		state_sensor = LOW;		
	}

	// dopisanie pomiaru do listy
	if ((state_sensor == LOW) && (working == LOW) && (finish == HIGH) && (dopisano == LOW))
	{
		czas_aktualny = (czas_konca - czas_startu);
		lista_czasow.push_front(czas_aktualny);
		dopisano = HIGH;
		while(lista_czasow.size() > int_max_times_nr) lista_czasow.pop_back();
	}

	wyswietlEkran(ekran);
	setOutputs();
}

void czasNaString(unsigned long czas)
{
	sprintf(bufmin, "%02d",(czas/1000 / 60000) % 60);
	sprintf(bufsek, "%02d",(czas/1000 / 1000) % 60);
	sprintf(bufms, "%03d",czas/1000 % 1000);
	sprintf(buf_akt_czas, "%2s:%2s.%3s",bufmin, bufsek, bufms);
}

void pokazLogo()
{
	// LOGO "Gymkhana Stoper" 
	oled.clear();
	oled.setFont(Cooper21);
	oled.setCursor(0, 0);
	oled.print("Gymkhana");
	oled.setCursor(25, 3);
	oled.print("Stoper");
	oled.setFont(Stang5x7);
	oled.setCursor(0, 7);
	oled.print(version); 
}

void pokazAktualnyCzas()
{
	oled.home();
	oled.setFont(lcdnums14x24); 
	oled.setCursor(0, 2);
	czasNaString(czas_aktualny);
	oled.print(buf_akt_czas);

	oled.setFont(Stang5x7);
	oled.setCursor(0, 0);
	if ((working == LOW) && (finish == LOW)) oled.print( "GOTOWY ");  
	if ((working == HIGH) && (finish == LOW)) oled.print("POMIAR "); 
	if ((working == LOW) && (finish == HIGH)) oled.print("META   ");   

	if (gp8_mode) {
		oled.setCursor(110, 0);
		oled.print("GP8");

		oled.setFont(Stang5x7);
		oled.setCursor(0, 7);
		oled.print("Okrazenie: ");
		oled.print(okrazenie);
		oled.print("/5");
	} else {
		oled.setCursor(110, 0);
		oled.print("   ");	
		oled.setCursor(0, 7);
		oled.print("              ");
	}

	/*
	if (best == HIGH) 
	{
		oled.setFont(Stang5x7);
		oled.setCursor(0, 7);
		oled.print("best: "); 
		oled.print(buf_best_czas);		
	}
	*/
}

void pokazArchiwalneCzasy()
{
	oled.setFont(Stang5x7);
	oled.home();

	if ((working == LOW) && (finish == LOW)) oled.print( "GOTOWY ");  
	if ((working == HIGH) && (finish == LOW)) oled.print("POMIAR "); 
	if ((working == LOW) && (finish == HIGH)) oled.print("META   "); 

	oled.setCursor(43, 0);
	czasNaString(czas_aktualny);
	oled.print(buf_akt_czas);

	int x = 25;
	int y = 2;

	for (SimpleList<long>::iterator itr = lista_czasow.begin(); itr != lista_czasow.end(); ++itr)
	{
		oled.setCursor(x, y);
		oled.print(y-1);
		oled.print(") ");
		czasNaString(*itr);
		oled.print(buf_akt_czas);
		if ((y==2) && (working == LOW) && (finish == HIGH)) oled.print(" <--");
		y++;
	}
}

void pokazPaging()
{
	if (ekran != 0){
	char bufor[4] = "0/0";
	sprintf(bufor, "%u/%u",ekran, max_ekran);

	oled.setFont(Stang5x7);
	oled.setCursor(110, 7);
	oled.print(bufor); 
	}
}

void wyswietlEkran(int screen)
{
	switch (screen)
	{
		case 0: // logo
		pokazLogo();
		delay(100);
		break;

		case 1: // aktualny czas
		pokazAktualnyCzas();
		delay(100);
		break;

		case 2: //archiwalne czasy
		pokazArchiwalneCzasy();
		delay(100);
		break;
	}
	pokazPaging();
}

void readInputs()
{
	pin_menu_deb.update();
	pin_reset_deb.update();
	pin_ex_reset_deb.update();
	pin_gp8_mode_deb.update();

	if (pin_menu_deb.fell() == HIGH) {state_menu = HIGH;}
	if (pin_reset_deb.fell() == HIGH) {state_reset = HIGH;}
	if (pin_ex_reset_deb.fell() == HIGH) {state_ex_reset = HIGH;}
	if (pin_gp8_mode_deb.read() == LOW) gp8_mode = HIGH; else gp8_mode = LOW;	
}

void setOutputs()
{
	if ((working == LOW) && (finish == LOW)) digitalWrite(pout_ready_led, LOW); else digitalWrite(pout_ready_led, HIGH);
	if ((working == HIGH) && (finish == LOW)) digitalWrite(pout_running_led, LOW); else digitalWrite(pout_running_led, HIGH);
	if (buzzer_on) digitalWrite(pout_buzzer, LOW); else digitalWrite(pout_buzzer, HIGH);

	if (buzzer_on) 
	{
		currentMillisBuzzer = millis();
		if (currentMillisBuzzer - previousMillisBuzzer > buzzer_time) {
			buzzer_on = LOW;
		}
	}

	if ((gp8_sensor_active == HIGH) && (gp8_dopisano == HIGH))
	{
		currentMillisGP8 = millis();
		if (currentMillisGP8 - previousMillisGP8 > czas_nieczulosci_gp8_ms) {
			gp8_sensor_active = LOW;
			gp8_dopisano = LOW;
		}

		//DIAG
		#ifdef DEBUG
		Serial.println(currentMillisGP8 - previousMillisGP8);	
		#endif
	}
}

void fsensor()
{
	// uruchomienie pomiaru
	if ((working == LOW) || ((finish == LOW) && (czas_aktualny >= czas_nieczulosci_ms)))
	{
		mils = timer2.get_count();
		state_sensor = HIGH;

	}
	else state_sensor = LOW;
	
	buzzer_switch_on = HIGH;
}

void fgp8sensor()
{
	if ((state_sensor == LOW) && (working == HIGH) && (finish == LOW) && (gp8_sensor_active == LOW)) gp8_sensor_active = HIGH;
}