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

	int ostatnieX = -1;
	int ostatnieY = -1;

	int licznik = 0;

	Mat video; //glowna klasa klatki kamery

	Mat linie; //tu przechowujemy linie

	//// Zmienne poza petla (end) ////

	while (1)
	{
		capture.read(video); //ciagle przechwytywanie nowej klatki kamery do wyswietlania linii i prostokatow

		//resetujemy linie i prostokat co jakiś czas (trzeba to zoptymalizować)
		licznik++;
		if (licznik = 6000000) 
		{
			licznik = 0;
			linie = Mat::zeros(video.size(), CV_8UC3); //tworzenie pustego obrazu o rozmiarze kamery w postaci RGB
		}

		//// Wykrywanie prostokatu (start)////

		Mat klatka_szara; //szara klatka do wykrywania prostokatu

		cvtColor(video, klatka_szara, CV_BGR2GRAY);

		// Mozna to zrobic na trzy sposoby:
		// 1) goodFeaturesToTrack: https://docs.opencv.org/3.0-beta/modules/imgproc/doc/feature_detection.html#goodfeaturestotrack
		// 2) Canny: https://docs.opencv.org/3.4.0/df/d0d/tutorial_find_contours.html - dopisac pozniej
		// 3) kod z tej strony: https://stackoverflow.com/questions/26583649/opencv-c-rectangle-detection-which-has-irregular-side - dopisac pozniej

		//// 1) goodFeaturesToTrack (start) ////

		namedWindow("Prostokat goodFeaturesToTrack", WINDOW_AUTOSIZE);

		Mat prostokat_goodFeaturesToTrack = klatka_szara;

		vector<Point2f> wierzcholki;
		
		Mat mask = Mat::zeros(video.size(), CV_8UC1);

		cv::goodFeaturesToTrack(prostokat_goodFeaturesToTrack, wierzcholki, 10, 0.01, 20., mask, 3, false, 0.04);

		//Rysowanie kolek reprezentujacych wierzcholki
		for (size_t i = 0; i < wierzcholki.size(); i++)
			cv::circle(prostokat_goodFeaturesToTrack, wierzcholki[i], 10, cv::Scalar(255.), -1);

		//// 1) goodFeaturesToTrack (end) ////

		//// Wykrywanie prostokatu (end)////

		//// Wykrywanie obiektu i rysownie linii (start)////

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
			int X = m10 / mPowierzchnia;
			int Y = m01 / mPowierzchnia;

			if (ostatnieX >= 0 && ostatnieY >= 0 && X >= 0 && Y >= 0)
				line(linie, Point(X, Y), Point(ostatnieX, ostatnieY), Scalar(0, 0, 255), 1); //rysowanie linii

			ostatnieX = X;
			ostatnieY = Y;
		}

		//// Wykrywanie obiektu i rysownie linii (start)////

		imshow("Obraz HSV", videoProgowe); //pokazywanie obrazu HSV

		imshow("Video", video + linie); //pokazywanie kamery z liniami

		imshow("Prostokat goodFeaturesToTrack", prostokat_goodFeaturesToTrack); //pokazywanie prostokata 1szej metody

		if (waitKey(1) == 27) break; //czekamy na klawisz 'esc'
	}

	return 0;
}
