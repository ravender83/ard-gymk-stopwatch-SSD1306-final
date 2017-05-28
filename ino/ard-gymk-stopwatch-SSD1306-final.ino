/**
 * Name:     Arduino - test wyswietlacza oled 128x64
 * Autor:    Piotr Ślęzak
 * Web:      https://github.com/ravender83
 * Date:     2017/05/25
*/
#define version "1.4.4"

#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <Bounce2.h>
#include <eRCaGuy_Timer2_Counter.h>
#include <SimpleList.h>

#define DEBUGoff
#define WYPELNIJoff

#define I2C_ADDRESS 0x3C // adres I2C 

#define pin_sensor 2		// wejście czujnika laserowego
#define pin_ex_reset 3		// wejście przycisku reset nożnego
#define pin_reset 10		// wejście przycisku reset na urządzeniu
#define pin_menu  11		// wejście przycisku menu na urządzeniu
#define int_max_times_nr 7

#define debounce_time_ms 10
#define max_ekran 2 // liczba dostepnych ekranow
#define logo_time 100 // czas wyswietlania logo w ms
#define czas_nieczulosci_ms 1*1000000 // czas przed upływem którego nie da się zresetować pomiaru

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

Bounce pin_reset_deb = Bounce(); 
Bounce pin_ex_reset_deb = Bounce(); 
Bounce pin_menu_deb = Bounce(); 

void setup()
{
	timer2.setup();

	// Ustawienie wejść, wyjść
	pinMode(pin_sensor, INPUT); // czujnik laserowy
	
	pinMode(pin_reset, INPUT_PULLUP); // przycisk reset
	pin_reset_deb.attach(pin_reset);
	pin_reset_deb.interval(debounce_time_ms);

	pinMode(pin_ex_reset, INPUT_PULLUP); // przycisk nożny reset
	pin_ex_reset_deb.attach(pin_ex_reset);
	pin_ex_reset_deb.interval(debounce_time_ms);

	pinMode(pin_menu, INPUT_PULLUP); // przycisk menu
	pin_menu_deb.attach(pin_menu);
	pin_menu_deb.interval(debounce_time_ms);

	oled.begin(&Adafruit128x64, I2C_ADDRESS);

	wyswietlEkran(0); // wyświetlenie logo
	delay(logo_time);
	oled.clear();
	ekran = 1; // aktualny czas

	lista_czasow.reserve(int_max_times_nr);
	lista_czasow.clear();

	attachInterrupt(digitalPinToInterrupt(pin_sensor), fsensor, FALLING);

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
		ekran=1;
		working = false;
		finish = false;
		best = false;
		czas_startu = 0;
		czas_konca = 0;
		czas_aktualny = 0;
		state_sensor = LOW;
		state_menu = LOW;
		state_reset = LOW;
		state_ex_reset = LOW;	
		dopisano = LOW;	
		oled.clear();
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
	if ((working == LOW) && (finish == LOW)) oled.print("READY  ");  
	if ((working == HIGH) && (finish == LOW)) oled.print("RUNNING"); 
	if ((working == LOW) && (finish == HIGH)) oled.print("FINISH ");   

	if (best == HIGH) 
	{
		oled.setFont(Stang5x7);
		oled.setCursor(0, 7);
		oled.print("best: "); 
		oled.print(buf_best_czas);		
	}
}

void pokazArchiwalneCzasy()
{
	oled.setFont(Stang5x7);
	oled.home();
	oled.print(" ==== ARCHIWALNE ==== ");
	int x = 25;
	int y = 1;

	for (SimpleList<long>::iterator itr = lista_czasow.begin(); itr != lista_czasow.end(); ++itr)
	{
		oled.setCursor(x, y);
		oled.print(y);
		oled.print(") ");
		czasNaString(*itr);
		oled.print(buf_akt_czas);
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

	if (pin_menu_deb.fell() == HIGH) {state_menu = HIGH;}
	if (pin_reset_deb.fell() == HIGH) {state_reset = HIGH;}
	if (pin_ex_reset_deb.fell() == HIGH) {state_ex_reset = HIGH;}
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
}