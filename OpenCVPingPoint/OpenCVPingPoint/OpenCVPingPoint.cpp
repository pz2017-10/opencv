#include "opencv2/highgui.hpp" //wyświetlanie okna
#include "opencv2/imgproc.hpp" //operacje na obrazie
#include <iostream>

#define ID_KAMERY 1 //zmienic w razie potrzeby

using namespace cv;
using namespace std;

int main(int argc, char** argv)
{
	//// Obsluga kamery (start) ////
	VideoCapture capture; //klasa przechwytywania video
	capture.open(ID_KAMERY); //otwieramy kamere
	if (!capture.isOpened()) // sprawdzamy czy podłączono kamerę w celu uniknięcia BSOD
	{
		cout << "Podlacz choc jedna kamere do komputera!" << endl << endl;
		return -1;
	}
	//// Obsluga kamery (end) ////

	//// Sterowanie wartościami HSV (start) ////

	namedWindow("Sterowanie"); //tworzenie okna sterowania

	int LowH = 0;
	int HighH = 179;

	int LowS = 0;
	int HighS = 255;

	int LowV = 0;
	int HighV = 255;

	createTrackbar("Niskie Hue", "Sterowanie", &LowH, 179);
	createTrackbar("Wysokie Hue", "Sterowanie", &HighH, 179);

	createTrackbar("Niskie Saturation", "Sterowanie", &LowS, 255);
	createTrackbar("Wysokie Saturation", "Sterowanie", &HighS, 255);

	createTrackbar("Niskie Value", "Sterowanie", &LowV, 255);
	createTrackbar("Wysokie Value", "Sterowanie", &HighV, 255);

	//// Sterowanie wartościami HSV (end) ////

	//// Zmienne poza petla (start) ////

	double ostatnieX = -1;
	double ostatnieY = -1;

	int licznik = 0;

	int czulosc = 100; //czulosc drugiej metody

	Mat video; //glowna klasa klatki kamery

	Mat linie; //tu przechowujemy linie

	//// Zmienne poza petla (end) ////

	while (1)
	{
		waitKey(1000); //opoznienie w milisekundach

		capture.read(video); //ciagle przechwytywanie nowej klatki kamery do wyswietlania linii i prostokatow

		//resetujemy linie co jakiś czas (trzeba to zoptymalizować)
		licznik++;
		if (licznik = 6000000) 
		{
			licznik = 0;
			linie = Mat::zeros(video.size(), CV_8UC3); //tworzenie pustego obrazu o rozmiarze kamery w postaci RGB
		}

		//// Wykrywanie prostokatu (start )////

		Mat klatka_szara; //szara klatka do wykrywania prostokatu, wykorzystywana we wszystkich metodach

		cvtColor(video, klatka_szara, CV_BGR2GRAY);

		// Mozna to zrobic na trzy sposoby:
		// 1) goodFeaturesToTrack: https://docs.opencv.org/3.0-beta/modules/imgproc/doc/feature_detection.html#goodfeaturestotrack
		// 2) Canny: https://docs.opencv.org/3.4.0/df/d0d/tutorial_find_contours.html
		// 3) minAreaRect z tej strony: https://stackoverflow.com/questions/26583649/opencv-c-rectangle-detection-which-has-irregular-side

		//// 1) goodFeaturesToTrack (start) ////

		namedWindow("Prostokat - 1sza metoda"); //tworzymy okno 1szej metody

		vector<Point2f> krawedzie1;

		Mat prostokat_goodFeaturesToTrack = video.clone();

		goodFeaturesToTrack(klatka_szara, krawedzie1, 4, 0.01, 10, Mat(), 3, 3, false, 0.04); //wykrywanie krawedzi

		//rysowanie prostokata na podstawie wykrytych krawedzi
		line(prostokat_goodFeaturesToTrack, krawedzie1[0], krawedzie1[1], Scalar(255, 0, 0));
		line(prostokat_goodFeaturesToTrack, krawedzie1[1], krawedzie1[2], Scalar(0, 255, 0));
		line(prostokat_goodFeaturesToTrack, krawedzie1[2], krawedzie1[3], Scalar(0, 0, 255));
		line(prostokat_goodFeaturesToTrack, krawedzie1[3], krawedzie1[0], Scalar(255, 255, 255));

		//// 1) goodFeaturesToTrack (end) ////

		//// 2) Canny (start) ////

		namedWindow("Prostokat - 2ga metoda"); //tworzymy okno 2giej metody

		createTrackbar("Czulosc:", "Prostokat - 2ga metoda", &czulosc, 255); //tworzymy suwak czulosci

		Mat canny_output;
		vector<vector<Point>> contours;
		vector<Vec4i> hierarchy;

		Canny(klatka_szara, canny_output, czulosc, czulosc * 2, 3);

		findContours(canny_output, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));

		Mat prostokat_Canny = video.clone();
		for (size_t i = 0; i< contours.size(); i++)
		{
			Scalar color = Scalar(255, 255, 255);
			drawContours(prostokat_Canny, contours, (int)i, color, 2, 8, hierarchy, 0, Point());
		}

		//// 2) Canny (end) ////

		//// 3) minAreaRect (start) ////

		namedWindow("Prostokat - 3cia metoda"); //tworzymy okno 3ciej metody

		Mat maska;
		threshold(klatka_szara, maska, 0, 255, CV_THRESH_BINARY_INV | CV_THRESH_OTSU); //czulosc

		vector<vector<Point>> kontury;
		vector<Vec4i> hierarchia;
		findContours(maska, kontury, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE); //tworzymy kontury

		int biggestContourIdx = -1;
		double biggestContourArea = 0;

		Mat prostokat_minAreaRect = video.clone(); //obraz prostokata 3cia metoda

		for (int i = 0; i < kontury.size(); i++)
		{
			drawContours(prostokat_minAreaRect, kontury, i, Scalar(0, 100, 0), 1, 8, hierarchia, 0, Point()); //zielone, pomocnicze linie, finalnie do usuniecia

			double ctArea = contourArea(kontury[i]);
			if (ctArea > biggestContourArea)
			{
					biggestContourArea = ctArea;
					biggestContourIdx = i;
			}
		}

		RotatedRect boundingBox = minAreaRect(kontury[biggestContourIdx]);

		Point2f krawedzie[4];
		boundingBox.points(krawedzie);

		line(prostokat_minAreaRect, krawedzie[0], krawedzie[1], Scalar(255, 255, 255));
		line(prostokat_minAreaRect, krawedzie[1], krawedzie[2], Scalar(255, 255, 255));
		line(prostokat_minAreaRect, krawedzie[2], krawedzie[3], Scalar(255, 255, 255));
		line(prostokat_minAreaRect, krawedzie[3], krawedzie[0], Scalar(255, 255, 255));

		//// 3) minAreaRect (end) ////

		//// Wykrywanie prostokatu (end) ////

		//// Wykrywanie obiektu i rysownie linii (start) ////

		Mat videoHSV;

		cvtColor(video, videoHSV, COLOR_BGR2HSV); //konwersja obrazu do postaci HSV (Hue, Saturation, Value)

		Mat videoProgowe;

		inRange(videoHSV, Scalar(LowH, LowS, LowV), Scalar(HighH, HighS, HighV), videoProgowe); //Tworzymy obraz na postawie aktualnych wartości suwaków

		//operacje morfologiczne (usuwamy małe obiekty i dziury)
		erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		dilate(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(videoProgowe, videoProgowe, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		Moments momenty = moments(videoProgowe); //https://docs.opencv.org/3.4.0/d8/d23/classcv_1_1Moments.html

		double m01 = momenty.m01;
		double m10 = momenty.m10;
		double mPowierzchnia = momenty.m00;

		if (mPowierzchnia > 10000)
		{
			//obliczanie pozycji obiektu
			double X = m10 / mPowierzchnia;
			double Y = m01 / mPowierzchnia;

			if (ostatnieX >= 0 && ostatnieY >= 0 && X >= 0 && Y >= 0) line(linie, Point((int)X, (int)Y), Point((int)ostatnieX, (int)ostatnieY), Scalar(0, 0, 255), 1); //rysowanie linii

			ostatnieX = X;
			ostatnieY = Y;
		}

		//// Wykrywanie obiektu i rysownie linii (end) ////

		imshow("Obraz HSV", videoProgowe); //pokazywanie obrazu HSV

		imshow("Linie", video + linie); //pokazywanie kamery z liniami

		imshow("Prostokat - 1sza metoda", prostokat_goodFeaturesToTrack); //pokazywanie prostokata 1szej metody

		imshow("Prostokat - 2ga metoda", prostokat_Canny); //pokazywanie prostokata 2giej metody

		imshow("Prostokat - 3cia metoda", prostokat_minAreaRect); //pokazywanie prostokata 3ciej metody
	}

	return 0;
}
