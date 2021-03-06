#define _CRT_SECURE_NO_WARNINGS // Pomijanie błędu kompilacji o funkcji sscanf używanej w komunikacji z aplikacją C#

#include <windows.h> // Obsługa potoków
#include <chrono> // Timer

// Biblioteka interfejsu graficznego "cvui" - https://dovyski.github.io/cvui/
#define CVUI_IMPLEMENTATION
#define CVUI_DISABLE_COMPILATION_NOTICES
#include "cvui.h"

// Identyfikator kamer podpiętych do komputera dla OpenCV - https://github.com/studiosi/OpenCVDeviceEnumerator
#include "DeviceEnumerator.h"
#include <map>

using namespace std;
using namespace cv;
using namespace chrono;

//~Zmienne globalne~//

VideoCapture capture; // Obiekt służący do przechwytywania kamery/pliku wideo

Mat video; // Obiekt surowych klatek z kamery/pliku wideo

Mat linie; // Obiekt obrazu na którym rysujemy linię toru piłki

Mat obszar; // Obiekt obrazu na którym rysujemy kalibrację stołu

Mat obszar_odbicie; // Obiekt obrazu na którym widzimy kółka reprezentujące aktualny stan setu

Mat okno_kalibracji; // Obiekt okna kalibracji

Mat videoPodglad; // Obiekt obrazu podglądu kalibracji piłki w tle okna kalibracji

char bufor[100]; // Bufor trzymający numer strony otrzymującej punkt, jej rozmiar musi być identyczny jak w aplikacji C# (0 - lewa strona stołu 1 - prawa strona stołu)

DWORD cbWritten; // Nieużywana zmienna wymagana do działania funkcji WriteLine - przesyłania sygnału do aplikacji C#

LPCTSTR NazwaPotoku = TEXT("\\\\.\\pipe\\myNamedPipe1"); // Nazwa potoku wysyłania numeru strony do aplikacji C# - musi być identyczna w obu aplikacjach

HANDLE Potok = CreateFile(NazwaPotoku, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL); // Obiekt tworzący potok

Mat videoHSV; // Obiekt obrazu w postaci HSV do kalibracja piłki

Mat videoProgowe; // Obiekt obrazu czarno-białego służący do kalibracji piłki, tworzony na podstawie videoHSV

char wejscie[1]; // Tablica wpisywanej komendy w oknie wiersza poleceń

int IDKamery; // Cyfra reprezentujaca ID kamery

bool GrajVideo = TRUE; // Boolowska zmienna służąca do zatrzymywania pliku wideo za pomocą przycisku "p"

bool skalibrowano = FALSE; // Boolowska zmienna mowiąca czy stół został skalibrowany

bool LiczPunkty = FALSE; // Boolowska zmienna mowiąca czy została zaznaczona opcja Licz punkty

// Początkowe wartości HSV 
int LowH = 0;
int LowS = 193; 
int LowV = 230; 

int HighH = 16; 
int HighS = 255;
int HighV = 255;

//int LowH = 2;
//int LowS = 190;
//int LowV = 138;

//int HighH = 21;
//int HighS = 255;
//int HighV = 255;

// Zmienne ostatniej pozycji piłki służące do obliczania punktu "poprzenia_pilka", początkowo nie znamy poprzedniej pozycji piłki, więc są zerowe
double ostatnieX = -1;
double ostatnieY = -1;

// Zmienne podawanej rozdzielczości kamery
int wysokosc;
int szerokosc;

// Zmienne istnienia odbicia w celu ominięcia ich powtórnego liczenia i rozpoczynania serwów
bool bylo_odbicie_lewe = FALSE;
bool bylo_odbicie_prawe = FALSE;
bool odb = FALSE;
bool poza = TRUE;
bool SerwRozpoczety = FALSE; // Zmienna boolowska mówiąca czy serw został rozpoczęty

// Punkty oraz prostokąty otrzymywane w wyniku kalibracji stołu
Point w_1; // Lewy wierzchołek stołu
Point w_2; // Prawy wierzchołek stołu
Point w_x; // Środek stołu
Point w_1x; // Lewy górny róg prostokąta lewej strony
Point w_2x; // Lewy górny róg prostokąta prawej strony
Point w_3x; // Prawy górny róg prostokąta prawej strony
Rect stol_lewa; // Prostokąt lewej strony stołu
Rect stol_prawa; // Prostokąt prawej strony stołu

// Punkty symbolizujące aktualną, oraz poprzednią pozycję piłki
Point pilka;
Point poprzednia_pilka;

int i = 0; // Licznik kliknięć procedury kalibracji stołu

// Zmienne wyrównania prostokątów i ich początkowe wartości
int wyrownanie1 = 60;
int wyrownanie2 = 70;

// Zmienne czułości odbicia obu stron stołu i ich początkowe wartości
int czulosc_lewa = 38;
int czulosc_prawa = 38;

// Zmienne boolowskie mówiące, której stronie stronu wysłać punkt do aplikacji C# wedle timera
bool dodaj_prawa = FALSE;
bool dodaj_lewa = FALSE;

int kierunek; // Zmienna symbolizująca kierunek piłki (0 - piłka leci w lewą stronę, 1 - piłka leci w prawą stronę)

void WyslijStrone(const char *strona) // Funkcja wysyłająca numer strony do aplikacji C# Ping Point (0 - lewa strona, 1 - prawa strona)
{
	sscanf(strona, "%s", bufor); // Wczytaj stronę do buforu
	// Zapisz stronę i wyślij do aplikacji C#
	WriteFile(Potok, bufor, 1, &cbWritten, NULL);
	memset(bufor, 0xCC, 100);
}

void kalibracja_stolu(int event, int x, int y, int flags, void* userdata) // Funkcja kalibracji stołu
{
	if (event == EVENT_LBUTTONDOWN && !skalibrowano) // Wykrywanie klikniecia lewym przyciskiem myszy w celu rozpoczęcia kalibracji
	{
		if (i == 0) // Pierwsze kliknięcie
		{
			obszar = Mat::zeros(video.size(), CV_8UC3); // Czyść obraz kalibracji 
			w_1 = Point(x, y); // Zapisujemy pierwszy punkt
			circle(obszar, w_1, 5, Scalar(0, 0, 255), -1); // Narysuj kółko czerwone - lewy wierzchołek stołu
		}

		if (i == 1) // drugie kliknięcie
		{
			w_2 = Point(x, y); // Zapisujemy drugi punkt

			// Obliczamy środek stołu
			w_x.x = (w_1.x + w_2.x) / 2;
			w_x.y = (w_1.y + w_2.y) / 2;

			// Rysujemy pozostałe kółka:
			circle(obszar, w_1, 5, Scalar(0, 0, 255), -1, LINE_AA); // Narysuj kółko czerwone raz jeszcze
			circle(obszar, w_2, 5, Scalar(0, 255, 255), -1, LINE_AA); // Narysuj kółko żółte - prawy wierzchołek stołu

			// Obliczamy i rysujemy dwa prostokąty reprezentujące strony stołu

			// Lewy górny róg prostokąta lewej strony
			w_1x.x = w_1.x;
			w_1x.y = w_1.y - wyrownanie1; 

			// Lewy górny róg prostokąta prawej strony
			w_2x.x = w_x.x; 
			w_2x.y = w_2.y - wyrownanie2; 

			Rect stol_lewa(w_1x, w_x); // Tworzymy obiekt prostokąta lewej strony, od jego lewego górnego rogu, do środka stołu
			Rect stol_prawa(w_2x, w_2); // Tworzymy obiekt prostokąta lewej strony, od jego lewego górnego rogu, do prawego wierzchołka stołu

			// Prawy górny róg prostokąta prawej strony obliczony z utworzonego prostokąta prawej strony
			w_3x.x = stol_prawa.x + stol_prawa.width;
			w_3x.y = stol_prawa.y;

			rectangle(obszar, stol_lewa, Scalar(255, 255, 0), 2, LINE_AA); // Rysuj prostokąt lewej strony
			rectangle(obszar, stol_prawa, Scalar(255, 0, 255), 2, LINE_AA); // Rysuj prostokąt prawej strony

			skalibrowano = TRUE; // Kończymy kalibrację
		}

		i++; // Zwiększamy licznik kliknięć
	}

	if (event == EVENT_RBUTTONDOWN) // Wykrywanie klikniecia prawym przyciskiem myszy w celu usunięcią kalibracji
	{
		i = 0; // Zerowanie licznika kliknięć
		skalibrowano = FALSE; // Ponów kalibrację
		LiczPunkty = FALSE; // Zatrzymaj liczenie punktów
		obszar = Mat::zeros(video.size(), CV_8UC3); // Czyść obraz kalibracji
	}

}

// Funkcja WinAPI służąca do odczytywania ścieżki do pliku, zwracamy w buforze ścieżkę do pliku
string GetFileName(const string & prompt) {
	char bufor[1024] = { 0 };
	OPENFILENAME ofns = { 0 };
	ofns.lStructSize = sizeof(ofns);
	ofns.lpstrFile = bufor;
	ofns.nMaxFile = 1024;
	ofns.lpstrTitle = prompt.c_str();
	GetOpenFileName(&ofns);
	return bufor;
}

int main()
{
	if ((Potok == NULL || Potok == INVALID_HANDLE_VALUE)) // Sprawdzamy utworzenie potoku
	{
		printf("\nBlad tworzenia potoku. Sprawdz czy aplikacja Ping Point C# jest uruchomiona.\n");
		system("PAUSE");
		return -1;
	}

	auto start = high_resolution_clock::now(); // Uruchamiamy timer

	// Uzyskaj wszystkie urządzenia wideo podpięte do komputera
	DeviceEnumerator enumerator;
	std::map<int, Device> devices = enumerator.getVideoDevicesMap(); 

	// Interfejs tekstowy wiersza poleceń
	printf("Witaj w programie OpenCVPingPoint!\n\n");
	printf("Lista podlaczonych kamer w komputerze:\n");
	for (auto const &device : devices) cout << "ID Kamery: " << device.first <<" - " << device.second.deviceName << endl << endl;
	printf("Wpisz prawidlowy ID Kamery, lub wpisz 'w', aby podac sciezke do pliku wideo, lub 'q' aby wyjsc, nastepnie kliknij ENTER:\n\n");

	scanf(" %c", &wejscie[0]);

	// Tryb kamery na żywo, podano cyfrę
	if (isdigit(wejscie[0]))
	{
		printf("\nPodaj szerokosc rozdzielczosci kamery (minimum 800):\n\n");
		cin >> szerokosc;
		if (cin.fail()) {
			printf("\nNiepoprawne wejscie.\n\n");
			system("PAUSE");
			return -1;
		}
		printf("\nPodaj wysokosc rozdzielczosci kamery (minimum 600):\n\n");
		cin >> wysokosc;
		if (cin.fail()) {
			printf("\nNiepoprawne wejscie.\n\n");
			system("PAUSE");
			return -1;
		}
		IDKamery = wejscie[0] - '0';
		capture.open(IDKamery); // Otwieramy kamerę
	}

	else if (wejscie[0] == 'q') return 0; // Wychodzimy z programu

	else if (wejscie[0] == 'w') // Otwieramy plik wideo
	{ 
		string nazwa = GetFileName("Wskaż plik wideo:");
		capture.open(nazwa);
	}
	 
	if (!capture.isOpened()) // Sprawdzamy czy poprawnie otwarto kamerę/plik
	{
		printf("\nPodano niepoprawny ID Kamery, niepoprawna sciezke do pliku wideo, lub niepoprawne wejscie.\n\n");
		system("PAUSE");
		return -1;
	}

	// Ustawiamy podaną rozdzielczość kamery
	capture.set(CAP_PROP_FRAME_WIDTH, szerokosc);
	capture.set(CAP_PROP_FRAME_HEIGHT, wysokosc);

	capture.read(video); // Przechwytujemy testową klatkę do tworzenia obiektów "obszar" oraz "okno_kalibracji"

	//~Aktywacja kalibracji stołu i obrazu na żywo~//

	obszar = Mat::zeros(video.size(), CV_8UC3); // Tworzymy czysty obraz kalibracji stołu o wielkości klatki kamery/filmu

	namedWindow("Obraz na żywo", WINDOW_NORMAL); // Tworzenie okna obrazu na żywo

	setMouseCallback("Obraz na żywo", kalibracja_stolu); // Aktywacja funkcji "kalibracja_stolu"

	//~Aktywacja okna kalibracji i kalibracji piłki~//

	okno_kalibracji = Mat::zeros(video.size(), CV_8UC3); // Tworzymy wnętrze okna kalibracji o wielkości klatki kamery/filmu

	namedWindow("Kalibracja", WINDOW_NORMAL); // Tworzenie okna ustawień kalibracji

	cvui::init("Kalibracja"); // Inicjalizacja interfejsu graficznego okna ustawień kalibracji

	obszar_odbicie = Mat::zeros(video.size(), CV_8UC3); // Tworzymy czysty obraz odbić o wielkości klatki wideo/kamery

	//~~Główna pętla programu: przechwytywanie obrazu na żywo, obraz kalibracji stołu, rysowanie toru piłki na żywo, zliczanie punktów na żywo~~//

	do
	{
		if (GrajVideo) capture.read(video); // Ciagle przechwytuj nową klatkę kamery

		if ((GetKeyState('P') & 0x8000)) GrajVideo = !GrajVideo; // Zatrzymaj/ponów plik video za pomocą przycisku "p"
				
		if (video.empty()) break; // Zamykamy program gdy film wideo się zakończy

		//~Kalibracja piłki~//

		cvtColor(video, videoHSV, COLOR_BGR2HSV); // Konwersja klatki do postaci HSV (Hue, Saturation, Value) w celu kalibracji piłki

		inRange(videoHSV, Scalar(LowH, LowS, LowV), Scalar(HighH, HighS, HighV), videoProgowe); // Tworzenie obrazu podglądu kalibracji piłki na podstawie aktualych wartości suwaków

		// Operacje usuwające szum: rozmycie Gaussa, erozja, dylatacja
		GaussianBlur(videoProgowe, videoProgowe, Size(25, 25), 0);
		erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		cvtColor(videoProgowe, videoPodglad, CV_GRAY2BGR); // Konwersja wymagana do wyświetlenie obrazu podglądu kalibracji piłki wraz z interfejsem graficznym okna kalibracji

		// Interfejs graficzny okna kalibracji
		cvui::image(okno_kalibracji, 0, 0, videoPodglad); // Kopiowanie obrazu podglądu kalibracji piłki do okna kalibracji
		
		cvui::beginColumn(okno_kalibracji, 5, 5, -1, -1, 5); // Pierwsza kolumna interfejsu
		cvui::text("Aby skalibrowac stol,", 0.5);
		cvui::text("kliknij lewy wierzcholek stolu w obrazie na zywo,", 0.5);
		cvui::text("nastepnie kliknij prawy wierzcholek stolu.", 0.5);
		cvui::text("Aby zresetowac, nacisnij prawy przycisk myszy.", 0.5);
		cvui::text("Oba prostokaty powinny miec", 0.5);
		cvui::text("ta sama wysokosc nad stolem,", 0.5);
		cvui::text("stykac sie przy siatce, oraz byc tak wysokie", 0.5);
		cvui::text("jak siatka", 0.5);
		cvui::text("Uzyj suwakow aby dostosowac wysokosc", 0.5);
		cvui::text("obu prostokatow:", 0.5);
		cvui::space(5);
		cvui::text("Wysokosc lewego prostokata:", 0.4);
		cvui::trackbar(350, &wyrownanie1, (int)0, (int)100, 2);
		cvui::text("Wysokosc prawego prostokata:", 0.4);
		cvui::trackbar(350, &wyrownanie2, (int)0, (int)100, 2);
		cvui::text("Reset kalibracji wymagany po zmianie suwakow.", 0.5);
		cvui::space(5);
		cvui::checkbox("Licz punkty - zaznacz tylko po kalibracji!", &LiczPunkty);
		cvui::space(5);
		cvui::text("Ustaw czulosc odbicia obu stron stolu i przetestuj", 0.5);
		cvui::text("czy odbicia sa pokazywane poprawnie", 0.5);
		cvui::text("Czulosc lewej strony:", 0.4);
		cvui::trackbar(350, &czulosc_lewa, (int)0, (int)100, 2);
		cvui::text("Czulosc prawej strony:", 0.4);
		cvui::trackbar(350, &czulosc_prawa, (int)0, (int)100, 2);
		cvui::text("*Program nalezy zamknac klawiszem ESC*", 0.5);
		cvui::endColumn();

		cvui::beginColumn(okno_kalibracji, 410, 5, -1, -1, 5); // Druga kolumna interfejsu
		cvui::text("Aby skalibrowac pilke,", 0.5);
		cvui::text("poloz ja nieruchomo na stole,", 0.5);
		cvui::text("i ustaw suwaki w taki sposob,", 0.5);
		cvui::text("aby pilka pozostala", 0.5);
		cvui::text("jedynym bialem obiektem w tle", 0.5);
		cvui::text("tego okna kalibracji.", 0.5);
		cvui::text("Nalezy przeprowadzic probne sety,", 0.5);
		cvui::text("aby potwierdzic widoczosc pilki", 0.5);
		cvui::text("podczas ruchu", 0.5);
		cvui::text("Powinna byc widoczna zielona linia", 0.5);
		cvui::text("podazajaca za pilka w obrazie na zywo", 0.5);
		cvui::space(5);
		cvui::text("Low Hue:", 0.4);
		cvui::trackbar(350, &LowH, (int)0, (int)179, 2);
		cvui::text("High Hue:", 0.4);
		cvui::trackbar(350, &HighH, (int)0, (int)179, 2);
		cvui::text("Low Saturation:", 0.4);
		cvui::trackbar(350, &LowS, (int)0, (int)255, 2);
		cvui::text("High Saturation:", 0.4);
		cvui::trackbar(350, &HighS, (int)0, (int)255, 2);
		cvui::text("Low Value:", 0.4);
		cvui::trackbar(350, &LowV, (int)0, (int)255, 2);
		cvui::text("High Value:", 0.4);
		cvui::trackbar(350, &HighV, (int)0, (int)255, 2);
		cvui::endColumn();

		cvui::imshow("Kalibracja", okno_kalibracji); // Wyświetlanie okna kalibracji

		//~Wykrywanie środka wykrytej wykrytej piłki oraz rysowanie jej toru~//

		linie = Mat::zeros(video.size(), CV_8UC3); // Tworzymy pusty obraz gdzie będziemy rysować linię toru piłki
			
		Moments momenty = moments(videoProgowe); // Moments - średnia ważona intensywności pikseli lub ich funkcja, preferowana metoda wykrywania środka dowolnego obiektu, tworzymy je z czarno-białego obrazu kalibracji piłki
		double m01 = momenty.m01;
		double m10 = momenty.m10;
		double mPowierzchnia = momenty.m00;
		double X = m10 / mPowierzchnia; // Pozycja X piłki
		double Y = m01 / mPowierzchnia; // Pozycja Y piłki

		bool dobryX = FALSE; // Zmienna boolowska mówiąca czy X jest poprawną liczbą

		pilka = Point((int)X, (int)Y); // Tworzymy punkt obecnej pozycji piłki

		if (ostatnieX >= 0 && ostatnieY >= 0 && X >= 0 && Y >= 0) line(linie, Point((int)X, (int)Y), Point((int)ostatnieX, (int)ostatnieY), Scalar(0, 255, 0), 4, LINE_AA); // Rysujemy linię gdy piłka jest w obszarze obrazu
			
		if (!isnan(X) && X >= 0) dobryX = TRUE; // Sprawdzamy czy X jest poprawną liczbą

		//~Liczenie punktów~//
		if (skalibrowano && LiczPunkty && dobryX) // Sprawdzamy czy skalibrowano stół, zaznaczono opcję "Licz punkty", oraz czy aktualna pozycja X jest poprawną liczbą
		{
			if (pilka.y > w_1x.y && pilka.x < w_2x.x && pilka.x > w_1x.x && pilka.y < w_1.y) // Piłka znajduje się na lewej stronie stołu
			{
				if (ostatnieX == -1 && ostatnieY == -1) poprzednia_pilka = w_1;

				// Obliczamy aktualny kierunek piłki
				int R = poprzednia_pilka.x - pilka.x;
				if (R > 0) kierunek = 0; // piłka porusza się w lewo
				else kierunek = 1; // piłka porusza się w prawo

				if (w_x.y - pilka.y <= czulosc_lewa) // Nastąpiło odbicie w lewej stronie stołu
				{
					start = high_resolution_clock::now(); // Rozpoczynamy timer

					if (!SerwRozpoczety)
					{
						if (kierunek == 1 && R < -3.0 && poza)
						{
							//cout << "pilka.x " << pilka.x << endl;
							//cout << "poprzednia_pilka.x " << poprzednia_pilka.x << endl;
							//cout << "LEWY SERW" << endl;
							SerwRozpoczety = TRUE;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
							circle(obszar_odbicie, Point(video.cols / 2, 25), 50, Scalar(255, 0, 255), -1, LINE_AA); // Różowe kółko - lewa strona rozpoczęła serw
							bylo_odbicie_lewe = TRUE;
							bylo_odbicie_prawe = FALSE;
							dodaj_lewa = FALSE;
							dodaj_prawa = TRUE;
						}
					}
					else 
					{
						if (!bylo_odbicie_lewe)
						{
							bylo_odbicie_lewe = TRUE;
							bylo_odbicie_prawe = FALSE;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
							circle(obszar_odbicie, Point(25, 25), 20, Scalar(0, 0, 255), -1, LINE_AA); // Czerwona kółko - nastąpiło odbicie po lewej stronie
							//printf("Odbicie lewa! Y: %f\n", Y);
							dodaj_lewa = FALSE;
							dodaj_prawa = TRUE;
						}

						else if (!odb)
						{
							//cout << "Wyslij prawa strona (1)" << endl;
							WyslijStrone("1");
							bylo_odbicie_lewe = FALSE;
							bylo_odbicie_prawe = FALSE;
							SerwRozpoczety = FALSE;
							poza = FALSE;
							//cout << "NIE MA SERWU" << endl;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
						}
					}
					odb = TRUE;
				}
				else
				{
					odb = FALSE;
					
				}
			}
			else
			{
				poza = TRUE;
			}

			if (pilka.y > w_2x.y && pilka.x > w_2x.x && pilka.x < w_3x.x && pilka.y < w_2.y) // Piłka znajduje się w prawym prostokącie
			{
				if (ostatnieX == -1 && ostatnieY == -1) poprzednia_pilka = w_2;

				// Obliczamy aktualny kierunek piłki
				int R = poprzednia_pilka.x - pilka.x;
				if (R > 0) kierunek = 0; // piłka porusza się w lewo
				else kierunek = 1; // piłka porusza się w prawo

				if (w_2.y - pilka.y <= czulosc_prawa) // Nastąpiło odbicie w prawej stronie stołu
				{
					start = high_resolution_clock::now(); // Rozpoczynamy timer

					if (!SerwRozpoczety)
					{
						if (kierunek == 0 && R > 3.0 && poza)
						{
							//cout << "pilka.x " << pilka.x << endl;
							//cout << "poprzednia_pilka.x " << poprzednia_pilka.x << endl;
							//cout << "PRAWY SERW" << endl;
							SerwRozpoczety = TRUE;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
							circle(obszar_odbicie, Point(video.cols / 2, 100), 50, Scalar(255, 0, 0), -1, LINE_AA); // Niebieskie kółko - prawa strona rozpoczęła serw
							bylo_odbicie_prawe = TRUE;
							bylo_odbicie_lewe = FALSE;
							dodaj_lewa = TRUE;
							dodaj_prawa = FALSE;
						}
					}
					else {
						if (!bylo_odbicie_prawe)
						{
							bylo_odbicie_lewe = FALSE;
							bylo_odbicie_prawe = TRUE;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
							circle(obszar_odbicie, Point(video.cols - 25, 20), 20, Scalar(0, 255, 255), -1, LINE_AA); // Żółte kółko - nastąpiło odbicie po prawej stronie
							//printf("Odbicie prawa! Y: %f\n", Y);
							dodaj_lewa = TRUE;
							dodaj_prawa = FALSE;
						}
						else if (!odb) {
							//cout << "Wyslij lewa strona (0)" << endl;
							WyslijStrone("0");
							bylo_odbicie_lewe = FALSE;
							bylo_odbicie_prawe = FALSE;
							SerwRozpoczety = FALSE;
							poza = FALSE;
							//cout << "NIE MA SERWU" << endl;
							obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
						}
					}
					odb = TRUE;
				}
				else 
				{
					odb = FALSE;
					
				}

			}
			else
				poza = TRUE;

			// Sytuacje gdy piłka znajduje się pod prostokątami stron stołu 
			if (pilka.y > w_1.y+5 && pilka.x > w_1.x && pilka.x < w_x.x && SerwRozpoczety) // Piłka jest pod lewym prostokątem, oraz serw się już rozpoczął
			{
				if (bylo_odbicie_lewe)
				{
					if (kierunek == 0)
					{
						//cout << "Wyslij lewa strona (0) POD STOLEM" << endl;
						WyslijStrone("0");
						bylo_odbicie_lewe = FALSE;
						bylo_odbicie_prawe = FALSE;
						SerwRozpoczety = FALSE;
						//cout << "NIE MA SERWU" << endl;
						obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
					}
		
				}

			}

			if (pilka.y > w_2.y+5 && pilka.x > w_x.x && pilka.x < w_2.x && SerwRozpoczety) // Piłka jest pod prawym prostokątem, oraz serw się już rozpoczął
			{
				if (kierunek == 1)
				{
					if (bylo_odbicie_prawe)
					{
						//cout << "Wyslij prawa strona (1) POD STOLEM" << endl;
						WyslijStrone("1");
						bylo_odbicie_lewe = FALSE;
						bylo_odbicie_prawe = FALSE;
						SerwRozpoczety = FALSE;
						//cout << "NIE MA SERWU" << endl;
						obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
					}
				}

			}

		}

		auto finish = high_resolution_clock::now(); // Zatrzymujemy timer

		// Funkcja wywoływana przez timer po upływie 2,5 sekundy od ostatniego odbicia
		if (duration_cast<milliseconds>(finish - start).count() > 2500) 
		{
			if (SerwRozpoczety)
			{
				// Dodaj punkt dla prawej strony
				if (dodaj_prawa) 
				{
					//cout << "timer wyslij prawa" << endl;
					WyslijStrone("1");
					bylo_odbicie_lewe = FALSE;
					bylo_odbicie_prawe = FALSE;
					SerwRozpoczety = FALSE;
					//cout << "NIE MA SERWU" << endl;
					obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
				}

				// Dodaj punkt dla lewej strony
				else if (dodaj_lewa) 
				{
					//cout << "timer wyslij lewa" << endl;
					WyslijStrone("0");
					bylo_odbicie_lewe = FALSE;
					bylo_odbicie_prawe = FALSE;
					SerwRozpoczety = FALSE;
					//cout << "NIE MA SERWU" << endl;
					obszar_odbicie = Mat::zeros(video.size(), CV_8UC3);
				}

			}
			dodaj_prawa = FALSE;
			dodaj_lewa = FALSE;
		}

		imshow("Obraz na żywo", video + linie + obszar + obszar_odbicie); // Pokazywanie obrazu na żywo wraz z nałożonym obrazem toru linii piłki, obrazem kalibracji stołu, oraz kółkami odbić

		if (waitKey(1) == 27) break; // Ciągle rysuj nowe klatki, oraz zakończ aplikację gdy naciśniemy klawisz ESC

	    // Tworzenie poprzeniej pozycji piłki na podstawie jej obecnej
		if (isnan(X) || X < 0) // Jeżeli pozycja X nie jest poprawna, zeruj poprzednią pozycję piłki
		{
			ostatnieX = -1;
			ostatnieY = -1;
		}
		else 
		{
			ostatnieX = X;
			ostatnieY = Y;
		}
		
		poprzednia_pilka = Point((int)ostatnieX, (int)ostatnieY); // Tworzymy punkt poprzedniej pozycji piłki

	} while (1); // Koniec głównej pętli

	CloseHandle(Potok); // Zamknięcie potoku po zamknięciu głównej pętli

	return 0;
}