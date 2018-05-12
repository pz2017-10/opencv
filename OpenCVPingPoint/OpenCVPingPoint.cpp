#define _CRT_SECURE_NO_WARNINGS // Pomijanie błędu kompilacji o funkcji sscanf używanej w komunikacji z aplikacją C#

#include <windows.h> // Obsługa potoków

// Biblioteka interfejsu graficznego "cvui" - https://dovyski.github.io/cvui/
#define CVUI_IMPLEMENTATION
#define CVUI_DISABLE_COMPILATION_NOTICES
#include "cvui.h"

// Identyfikator urządzeń dla OpenCV - https://github.com/studiosi/OpenCVDeviceEnumerator
#include "DeviceEnumerator.h"
#include <map>

using namespace cv;
using namespace std;

//~Zmienne globalne~//

VideoCapture capture; // Obiekt służący do przechwytywania kamery/pliku video

Mat video; // Obiekt surowych klatek z kamery/pliku video

Mat linie; // Obiekt obrazu na którym rysujemy linię piłki

Mat obszar; // Obiekt obrazu na którym rysujemy kalibrację stołu

Mat okno_kalibracji; // Obiekt okna kalibracji

Mat videoPodglad; // Obiekt obrazu podglądu kalibracji piłki

char bufor[100]; // Bufor trzymający numer strony otrzymującej punkt, jej rozmiar musi być identyczny jak w aplikacji C# (0 - lewa strona stołu 1 - prawa strona stołu)

DWORD cbWritten; // Nieużywana zmienna wymagana do działania funkcji WriteLine

LPCTSTR NazwaPotoku = TEXT("\\\\.\\pipe\\myNamedPipe1"); // Nazwa potoku wysyłania numeru strony do aplikacji C# - musi być identyczna w obu aplikacjach

HANDLE Potok = CreateFile(NazwaPotoku, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL); // Obiekt tworzący potok

Mat videoHSV; // Obiekt obrazu w postaci HSV do kalibracja piłki

Mat videoProgowe; // Obiekt obrazu czarno-białego służący do kalibracji piłki, tworzony na podstawie videoHSV

char wejscie[1]; // Tablica wpisywanych komend

int IDKamery; // Cyfra reprezentujaca ID kamery

bool GrajVideo = true; // Boolowska zmienna służąca do zatrzymywania pliku video za pomocą przycisku "p"

bool skalibrowano = FALSE; // Boolowska zmienna mowiąca czy stół został skalibrowany

bool LiczPunkty = FALSE; // Boolowska zmienna mowiąca czy należy liczyć punkty

// Początkowe wartości HSV 
int LowH = 0; //179 finalnie
int LowS = 193; //255 finalnie
int LowV = 230; //255 finalnie

int HighH = 16; //179 finalnie
int HighS = 255;
int HighV = 255;

// Zmienne ostatniej pozycji piłki służące do rysowania linii, początkowo nie znamy pozycji piłki, więc są zerowe
double ostatnieX = -1;
double ostatnieY = -1;

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

int i = 0; // Licznik kliknięć procedury kalibracji

// Zmienne wyrównania prostokątów i ich początkowe wartości
int wyrownanie1 = 40;
int wyrownanie2 = 56;

void WyslijStrone(const char *strona) // Funkcja wysyłająca numer strone do aplikacji C# Ping Point
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
		if (i == 0)
		{
			obszar = Mat::zeros(video.size(), CV_8UC3); // Czyść obraz kalibracji 

			w_1 = Point(x, y); // Zapisujemy pierwszy punkt
			circle(obszar, w_1, 5, Scalar(0, 0, 255), -1); // Narysuj kółko czerwone - lewa strona
		}

		if (i == 1)
		{
			w_2 = Point(x, y); // Zapisujemy drugi punkt

			line(obszar, w_1, w_2, Scalar(255, 255, 255), 2, LINE_AA); // Rysujemy białą linię

			// Obliczamy środek stołu
			w_x.x = (w_1.x + w_2.x) / 2;
			w_x.y = (w_1.y + w_2.y) / 2;

			// Rysujemy pozostałe kółka:
			circle(obszar, w_1, 5, Scalar(0, 0, 255), -1, LINE_AA); // Narysuj kółko czerwone raz jeszcze
			circle(obszar, w_2, 5, Scalar(0, 255, 255), -1, LINE_AA); // żółte - prawa strona

			// Obliczamy i rysujemy dwa prostokąty reprezentujące strony stołu

			// Lewy górny róg prostokąta lewej strony
			w_1x.x = w_1.x;
			w_1x.y = w_1.y - wyrownanie1; 

			// Lewy górny róg prostokąta prawej strony
			w_2x.x = w_x.x; 
			w_2x.y = w_2.y - wyrownanie2; 

			Rect stol_lewa(w_1x, w_x); // Tworzymy obiekt prostokąta lewej strony, od lewego górnego rogu, do środka stołu
			Rect stol_prawa(w_2x, w_2); // Tworzymy obiekt prostokąta lewej strony, od lewego górnego rogu, do prawego wierzchołka stołu

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

// Funkcja WinAPI służąca do odczytywania ścieżki do pliku
string  GetFileName(const string & prompt) {
	const int BUFSIZE = 1024;
	char buffer[BUFSIZE] = { 0 };
	OPENFILENAME ofns = { 0 };
	ofns.lStructSize = sizeof(ofns);
	ofns.lpstrFile = buffer;
	ofns.nMaxFile = BUFSIZE;
	ofns.lpstrTitle = prompt.c_str();
	GetOpenFileName(&ofns);
	return buffer;
}

int main()
{
	// Uzyskaj wszystkie urządzenia wideo podpięte do komputera
	DeviceEnumerator enumerator;
	std::map<int, Device> devices = enumerator.getVideoDevicesMap(); 

	// Interfejs tekstowy
	printf("Witaj w programie OpenCVPingPoint!\n\n");
	printf("Lista podlaczonych kamer w komputerze:\n");
	for (auto const &device : devices) cout << "ID Kamery: " << device.first <<" - " << device.second.deviceName << endl << endl;
	printf("Wpisz prawidlowy ID Kamery, lub wpisz 'w', aby podac sciezke do pliku wideo, lub 'q' aby wyjsc:\n\n");

	scanf(" %c", &wejscie[0]);

	// Tryb kamery, podano cyfrę
	if (isdigit(wejscie[0]))
	{
		IDKamery = wejscie[0] - '0';
		capture.open(IDKamery); // Otwieramy kamerę
	}

	else if (wejscie[0] == 'q') return 0; // Wychodzimy

	else if (wejscie[0] == 'w') { // Otwieramy plik wideo
		string nazwa = GetFileName("Wskaż plik wideo:");
		capture.open(nazwa);
	}
	 
	if (!capture.isOpened()) // Sprawdzamy czy poprawnie otwarto kamerę/plik
	{
		printf("\nPodano niepoprawny ID Kamery, lub niepoprawna sciezke do pliku wideo.\n\n");
		system("PAUSE");
		return -1;
	}

	// Ustawienia dla kamery używanej w testach - Microsoft LifeCam HD-3000, są to minimalne wartości obsługiwane przez interfejs
	capture.set(CAP_PROP_FRAME_WIDTH, 1280);
	capture.set(CAP_PROP_FRAME_HEIGHT, 720);

	capture.read(video); // Testowa klatka do tworzenia obiektów "obszar" oraz "okno_kalibracji"

	//~Aktywacja kalibracji stołu i obrazu na żywo~//

	obszar = Mat::zeros(video.size(), CV_8UC3); // Tworzymy czysty obraz kalibracji stołu o wielkości klatki kamery/filmu

	namedWindow("Obraz na żywo", WINDOW_NORMAL); // Tworzenie okna obrazu na żywo

	setMouseCallback("Obraz na żywo", kalibracja_stolu); // Aktywacja funkcji "kalibracja_stolu"

	//~Aktywacja okna kalibracji i kalibracji piłki~//

	okno_kalibracji = Mat::zeros(video.size(), CV_8UC3); // Tworzymy wnętrze okna kalibracji o wielkości klatki kamery/filmu

	namedWindow("Kalibracja", WINDOW_NORMAL); // Tworzenie okna ustawień kalibracji

	cvui::init("Kalibracja"); // Inicjalizacja interfejsu graficznego okna ustawień kalibracji
	
	//~~Główna pętla programu: obraz na żywo, obraz kalibracji stołu, rysowanie toru piłki na żywo, zliczanie punktów na żywo~~//

	//if ((Potok == NULL || Potok == INVALID_HANDLE_VALUE)) // Sprawdzanie utworzenia potoku
	//{
	//	printf("Blad tworzenia potoku. Sprawdz czy aplikacja Ping Point C# jest uruchomiona.\n");
	//	return -1;
	//}

	//else
	//{
		do
		{
			if ((GetKeyState('P') & 0x8000)) GrajVideo = !GrajVideo; // Zatrzymaj/ponów plik video za pomocą przycisku "p"
				
			if (GrajVideo) capture.read(video); // Ciagle przechwytuj nową klatkę kamery

			//~Kalibracja piłki~//

			cvtColor(video, videoHSV, COLOR_BGR2HSV); // Konwersja klatki do postaci HSV (Hue, Saturation, Value) w celu kalibracji piłki

			inRange(videoHSV, Scalar(LowH, LowS, LowV), Scalar(HighH, HighS, HighV), videoProgowe); // Tworzenie obrazu podglądu kalibracji piłki na podstawie aktualych wartości suwaków

			// Operacje usuwające szum: rozmycie Gaussa, erozja, dylatacja
			GaussianBlur(videoProgowe, videoProgowe, Size(25, 25), 0);
			erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			cvtColor(videoProgowe, videoPodglad, CV_GRAY2BGR); // Konwersja wymagana do wyświetlenie obrazu podglądu kalibracji piłki wraz z interfejsem graficznym

			// Interfejs graficzny okna kalibracji
			cvui::image(okno_kalibracji, 0, 0, videoPodglad); // Kopiowanie obrazu podglądu kalibracji piłki do okna kalibracji
		
			cvui::beginColumn(okno_kalibracji, 5, 5, -1, -1, 5); // Pierwsza kolumna interfejsu
			cvui::text("Aby skalibrowac stol,", 0.5);
			cvui::text("kliknij lewy wierzcholek stolu w obrazie na zywo", 0.5);
			cvui::text("nastepnie kliknij prawy wierzcholek stolu.", 0.5);
			cvui::text("Aby zresetowac, nacisnij prawy przycisk myszy.", 0.5);
			cvui::text("Oba prostokaty powinny miec", 0.5);
			cvui::text("ta sama wysokosc nad stolem,", 0.5);
			cvui::text("oraz stykac sie przy siatce", 0.5);
			cvui::text("Uzyj suwakow aby dostosowac oba prostokaty:", 0.5);
			cvui::space(5);
			cvui::text("Lewy prostokat:", 0.4);
			cvui::trackbar(350, &wyrownanie1, (int)0, (int)100, 2);
			cvui::text("Prawy prostokat:", 0.4);
			cvui::trackbar(350, &wyrownanie2, (int)0, (int)100, 2);
			cvui::text("Reset kalibracji wymagany po zmianie suwakow.", 0.5);
			cvui::space(5);
			cvui::checkbox("Licz punkty - dziala tylko po kalibracji!", &LiczPunkty);
			cvui::space(5);
			cvui::text("Wskazowki:", 0.5); // TODO: Dopisać wskazówki
			cvui::text("*Kamera musi miec rozdzielczosc", 0.5);
			cvui::text("conajmniej 800x600", 0.5);
			cvui::text("*Zapoznaj sie z dokumentacja", 0.5);
			cvui::text("w celu uzyskania najlepszych rezultatow", 0.5);
			cvui::text("*Program nalezy zamknac klawiszem ESC", 0.5);
			cvui::endColumn();

			cvui::beginColumn(okno_kalibracji, 410, 5, -1, -1, 5); // Druga kolumna interfejsu
			cvui::text("Aby skalibrowac pilke,", 0.5);
			cvui::text("ustaw suwaki w taki sposob,", 0.5);
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

			double X = m10 / mPowierzchnia;
			double Y = m01 / mPowierzchnia;

			pilka = Point((int)X, (int)Y); // Tworzymy punkt obecnej pozycji piłki

			if (ostatnieX >= 0 && ostatnieY >= 0 && X >= 0 && Y >= 0) line(linie, Point((int)X, (int)Y), Point((int)ostatnieX, (int)ostatnieY), Scalar(0, 255, 0), 4, LINE_AA); // Rysujemy linię gdy piłka jest w zasięgu obrazu
			
			// Koniec poprzedniej linii staje się początkiem nowej
			ostatnieX = X;
			ostatnieY = Y;

			poprzednia_pilka = Point((int)ostatnieX, (int)ostatnieY); // Tworzymy punkt poprzedniej pozycji piłki

			imshow("Obraz na żywo", video + linie + obszar); // Pokazywanie obrazu na żywo wraz z nałożonym obrazem toru linii piłki, oraz obrazem kalibracji stołu

			//~Liczenie punktów~//
			if (skalibrowano && LiczPunkty)
			{
				if (pilka.y > w_1x.y && pilka.x < w_2x.x && pilka.x > w_1x.x && pilka.y < w_1.y) // Piłka znajduje się w lewym prostokącie
				{
					printf("Lewa strona! ");
					printf("Y: %f\n", Y);
				}

				if (pilka.y > w_2x.y && pilka.x > w_2x.x && pilka.x < w_3x.x && pilka.y < w_2.y) // Piłka znajduje się w prawym prostokącie
				{
					printf("Prawa strona! ");
					printf("Y: %f\n", Y);
				}
			}

			if (waitKey(1) == 27) break; // Ciągle rysuj nowe klatki, oraz zakończ aplikację gdy naciśniemy klawisz ESC

		} while (1);

		CloseHandle(Potok); // Zamknięcie potoku po zamknięciu głównej pętli
	//}

	return 0;
}

