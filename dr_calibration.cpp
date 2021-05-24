#include "dr_calibration.h"

using namespace cv;
using namespace std;

double DR_Calibration::computeReprojectionErrors(
	const vector<vector<Point3f> >& objectPoints,
	const vector<vector<Point2f> >& imagePoints,
	const vector<Mat>& rvecs, const vector<Mat>& tvecs,
	const Mat& cameraMatrix, const Mat& distCoeffs,
	vector<float>& perViewErrors)
{
	vector<Point2f> imagePoints2;
	int i, totalPoints = 0;
	double totalErr = 0, err;
	perViewErrors.resize(objectPoints.size());

	for (i = 0; i < (int)objectPoints.size(); i++)
	{
		projectPoints(Mat(objectPoints[i]), rvecs[i], tvecs[i],
			cameraMatrix, distCoeffs, imagePoints2);
		err = norm(Mat(imagePoints[i]), Mat(imagePoints2), NORM_L2);
		int n = (int)objectPoints[i].size();
		perViewErrors[i] = (float)std::sqrt(err*err / n);
		totalErr += err * err;
		totalPoints += n;
	}

	return std::sqrt(totalErr / totalPoints);
}

void DR_Calibration::calcChessboardCorners(Size boardSize, float squareSize,
	vector<Point3f>& corners, Pattern patternType)
{
	corners.resize(0);

	switch (patternType)
	{
	case CHESSBOARD:
	case CIRCLES_GRID:
		for (int i = 0; i < boardSize.height; i++)
			for (int j = 0; j < boardSize.width; j++)
				corners.push_back(Point3f(float(j*squareSize),
					float(i*squareSize), 0));
		break;

	case ASYMMETRIC_CIRCLES_GRID:
		for (int i = 0; i < boardSize.height; i++)
			for (int j = 0; j < boardSize.width; j++)
				corners.push_back(Point3f(float((2 * j + i % 2)*squareSize),
					float(i*squareSize), 0));
		break;

	default:
		CV_Error(Error::StsBadArg, "Unknown pattern type\n");
	}
}


void DR_Calibration::createCalibBoard(string fileDir)
{
	Mat img(2592, 2048, CV_8UC1, Scalar::all(0));//初始化img矩阵，全黑

	int cube = 360;
	for (int j = 0; j < img.rows; j++)
	{
		uchar *data = img.ptr<uchar>(j);
		for (int i = 0; i < img.cols; i += 1)
		{
			if ((i / cube + j / cube) % 2)//符合此规律的像素，置255
			{
				data[i] = 255;
			}
		}
	}
	imshow("img", img);
	imwrite(fileDir, img);//保存图片到默认路径
	waitKey(0);
}

bool DR_Calibration::runCalibration(vector<vector<Point2f> > imagePoints,
	Size imageSize, Size boardSize, Pattern patternType,
	float squareSize, float aspectRatio,
	int flags, Mat& cameraMatrix, Mat& distCoeffs,
	vector<Mat>& rvecs, vector<Mat>& tvecs,
	vector<float>& reprojErrs,
	double& totalAvgErr)
{
	double rms;

	cameraMatrix = Mat::eye(3, 3, CV_64F);
	if (flags & CALIB_FIX_ASPECT_RATIO)
		cameraMatrix.at<double>(0, 0) = aspectRatio;

	distCoeffs = Mat::zeros(8, 1, CV_64F);

	vector<vector<Point3f> > objectPoints(1);
	calcChessboardCorners(boardSize, squareSize, objectPoints[0], patternType);

	objectPoints.resize(imagePoints.size(), objectPoints[0]);


	rms = calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix,
		distCoeffs, rvecs, tvecs, flags);

	//double rms = calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix,
	//	distCoeffs, rvecs, tvecs, CALIB_USE_INTRINSIC_GUESS);

	///*|CALIB_FIX_K3*/|CALIB_FIX_K4|CALIB_FIX_K5);

	cout << "内参矩阵:" << endl << cameraMatrix << endl;

	printf("RMS error reported by calibrateCamera: %g\n", rms);

	bool ok = checkRange(cameraMatrix) && checkRange(distCoeffs);

	totalAvgErr = computeReprojectionErrors(objectPoints, imagePoints,
		rvecs, tvecs, cameraMatrix, distCoeffs, reprojErrs);

	return ok;
}


void DR_Calibration::saveCameraParams(const string& filename,
	Size imageSize, Size boardSize,
	float squareSize, float aspectRatio, int flags,
	const Mat& cameraMatrix, const Mat& distCoeffs,
	const vector<Mat>& rvecs, const vector<Mat>& tvecs,
	const vector<float>& reprojErrs,
	const vector<vector<Point2f> >& imagePoints,
	double totalAvgErr)
{
	FileStorage fs(filename, FileStorage::WRITE);
	if (!fs.isOpened()) {
		std::cout << "Can not create \"" << filename << "\"." << std::endl;	return;
	}

	if (!cv::utils::fs::exists(filename))
		cv::error(cv::Error::StsBadArg, "file create failed.", __FUNCTION__, __FILE__, __LINE__);

	time_t tt;
	time(&tt);
	struct tm *t2 = new struct tm();
	localtime_s(t2, &tt);
	char buf[1024];
	strftime(buf, sizeof(buf) - 1, "%c", t2);

	fs << "calibration_time" << buf;

	if (!rvecs.empty() || !reprojErrs.empty())
		fs << "nframes" << (int)std::max(rvecs.size(), reprojErrs.size());
	fs << "image_width" << imageSize.width;
	fs << "image_height" << imageSize.height;
	fs << "board_width" << boardSize.width;
	fs << "board_height" << boardSize.height;
	fs << "square_size" << squareSize;

	if (flags & CALIB_FIX_ASPECT_RATIO)
		fs << "aspectRatio" << aspectRatio;

	if (flags != 0)
	{
		sprintf_s(buf, "flags: %s%s%s%s",
			flags & CALIB_USE_INTRINSIC_GUESS ? "+use_intrinsic_guess" : "",
			flags & CALIB_FIX_ASPECT_RATIO ? "+fix_aspectRatio" : "",
			flags & CALIB_FIX_PRINCIPAL_POINT ? "+fix_principal_point" : "",
			flags & CALIB_ZERO_TANGENT_DIST ? "+zero_tangent_dist" : "");
		//cvWriteComment( *fs, buf, 0 );
	}

	fs << "flags" << flags;

	fs << "camera_matrix" << cameraMatrix;
	fs << "distortion_coefficients" << distCoeffs;

	fs << "avg_reprojection_error" << totalAvgErr;
	if (!reprojErrs.empty())
		fs << "per_view_reprojection_errors" << Mat(reprojErrs);

	if (!rvecs.empty() && !tvecs.empty())
	{
		CV_Assert(rvecs[0].type() == tvecs[0].type());
		Mat bigmat((int)rvecs.size(), 6, rvecs[0].type());
		for (int i = 0; i < (int)rvecs.size(); i++)
		{
			// {r, t}
			Mat r = bigmat(Range(i, i + 1), Range(0, 3));
			Mat t = bigmat(Range(i, i + 1), Range(3, 6));

			CV_Assert(rvecs[i].rows == 3 && rvecs[i].cols == 1);
			CV_Assert(tvecs[i].rows == 3 && tvecs[i].cols == 1);
			//*.t() is MatExpr (not Mat) so we can use assignment operator
			r = rvecs[i].t();	// 旋转向量 -> Rodrigues变换 -> 旋转矩阵  
			t = tvecs[i].t();	// 旋转矩阵

			Mat R;

			cv::Rodrigues(rvecs[i], R);

			fs << "extrinsic_R" + to_string(i) << R;

			fs << "extrinsic_T" + to_string(i) << tvecs[i];

		}

		fs << "extrinsic" << bigmat;

		std::swap(bigmat, extrinsicsBigMat);
	}

	/////  0731 存储检测棋盘结果  1  成功， 0  失败  ==》 对应1的菜读取机械手姿态数据
	Mat matFoundCheeseBoard(1, foundCheeseBoardVec.size(), CV_32S, foundCheeseBoardVec.data());
	fs << "found_cheese_board" << matFoundCheeseBoard;


	if (!imagePoints.empty())
	{
		Mat imagePtMat((int)imagePoints.size(), (int)imagePoints[0].size(), CV_32FC2);
		for (int i = 0; i < (int)imagePoints.size(); i++)
		{
			Mat r = imagePtMat.row(i).reshape(2, imagePtMat.cols);
			Mat imgpti(imagePoints[i]);
			imgpti.copyTo(r);
		}
		fs << "image_points" << imagePtMat;
	}
	fs.release();

	std::cout << "DR_Calibration done, save datas in file " << filename << endl << endl;
}

bool DR_Calibration::runAndSave(const string& outputFilename,
	const vector<vector<Point2f> >& imagePoints,
	Size imageSize, Size boardSize, Pattern patternType, float squareSize,
	float aspectRatio, int flags, Mat& cameraMatrix,
	Mat& distCoeffs, bool writeExtrinsics, bool writePoints)
{
	vector<Mat> rvecs, tvecs;
	vector<float> reprojErrs;
	double totalAvgErr = 0;

	bool ok = runCalibration(imagePoints, imageSize, boardSize, patternType, squareSize,
		aspectRatio, flags, cameraMatrix, distCoeffs,
		rvecs, tvecs, reprojErrs, totalAvgErr);

	printf("%s. avg reprojection error = %.2f\n",
		ok ? "DR_Calibration succeeded" : "DR_Calibration failed",
		totalAvgErr);

	if (ok)
		saveCameraParams(outputFilename, imageSize,
			boardSize, squareSize, aspectRatio,
			flags, cameraMatrix, distCoeffs,
			writeExtrinsics ? rvecs : vector<Mat>(),
			writeExtrinsics ? tvecs : vector<Mat>(),
			writeExtrinsics ? reprojErrs : vector<float>(),
			writePoints ? imagePoints : vector<vector<Point2f> >(),
			totalAvgErr);
	return ok;
}

bool DR_Calibration::readCameraParameters(const std::string & filename, cv::Mat & camMatrix, cv::Mat & distCoefs)
{
	FileStorage fs(filename, FileStorage::READ);
	if (!fs.isOpened()) {
		std::cout << "open \"" << filename << "\" failed." << std::endl;
		return false;
	}
	camMatrix = fs["camera_matrix"].mat();
	distCoefs = fs["distortion_coefficients"].mat();
	fs.release();

	return true;
}

bool DR_Calibration::doCalibration()
{

	vector<String> imageList;
	std::string path = _imgsDir;

	try
	{
		cv::glob(path, imageList);
	}
	catch (const std::exception&)
	{
		std::cout << "read calibration image error!\n";
		return false;
	}

	if (imageList.size() == 0) {
		std::cout << "no images." << std::endl;	return false;
		return false;
	}

	int nframes = (int)imageList.size();

	this->foundCheeseBoardVec.resize(nframes);

	if (showUndistorted) {
		resizeWindow("Image View", { imageSize.width,imageSize.height });
	}

	std::cout << "Process: ... ... ";
	for (int i = 0;; i++) 
	{
		Mat view;

		std::cout << "\n";
		if (i < (int)imageList.size()) {
			std::cout << i << "  " << imageList[i];
			view = imread(imageList[i], 1);
		}

		if (view.empty())  // use previous imgs to calibration
		{
			std::cout << "Calculating ... ...\n";
			if (imagePoints.size() > 0)
				runAndSave(_outputFilename, imagePoints, imageSize,
					_boardSize, _pattern, _squareSize, _aspectRatio,
					flags, cameraMatrix, distCoeffs,
					writeExtrinsics, writePoints);
			break;
		}
		imageSize = view.size();
		if (view.channels() == 3) 
		{
			cvtColor(view, view, COLOR_BGR2GRAY);
		}
		if (flipVertical)	
			flip(view, view, 0);

		vector<Point2f> pointbuf;	//角点

		bool isFound;
		switch (_pattern)
		{

		case DR_Calibration::CHESSBOARD:
			isFound = findChessboardCorners(view, _boardSize, pointbuf,
				CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_FAST_CHECK | CALIB_CB_NORMALIZE_IMAGE);
			break;

		case DR_Calibration::CIRCLES_GRID:					//对称
			isFound = findCirclesGrid(view, _boardSize, pointbuf, CALIB_CB_SYMMETRIC_GRID);
			break;

		case DR_Calibration::ASYMMETRIC_CIRCLES_GRID:		//不对称
			//cv::bitwise_not(view, view);	//反转图片颜色
			isFound = findCirclesGrid(view, _boardSize, pointbuf, CALIB_CB_ASYMMETRIC_GRID);
			break;

		default:
			break;
		}

		if (isFound)
			cornerSubPix(view, pointbuf, _boardSize, Size(-1, -1),
				TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));

		if (_mode == CALIBRATED && isFound) {
			imagePoints.push_back(pointbuf);
			cout << ", Success";
			foundCheeseBoardVec[i] = 1;
		}
		else {
			cout << ", Fail";
			foundCheeseBoardVec[i] = 0;

		}

		if (isFound)	
			drawChessboardCorners(view, _boardSize, Mat(pointbuf), isFound);

		string msg = _mode == CAPTURING ? "100/100" : _mode == CALIBRATED ? "Calibrated" : "Press 'g' to start";
		int baseLine = 0;
		Size textSize = getTextSize(msg, 1, 1, 1, &baseLine);
		Point textOrigin(view.cols - 2 * textSize.width - 10, view.rows - 2 * baseLine - 10);

		putText(view, format("%d/%d", (int)imagePoints.size(), nframes), textOrigin, 1, 1, _mode == CALIBRATED ? Scalar(0, 0, 255) : Scalar(0, 255, 0));
		
		if (_saveResImg)
		{
			imwrite(_imgsDir + std::to_string(i) + ".png", view);//保存图片到默认路径
		}

		cv::resize(view, view, cv::Size(imageSize.width / 3, imageSize.height / 3));
		//cv::moveWindow("Running Calibration", 0, 0);

		imshow("Running Calibration", view);
		if (waitKey(300) == 27)	break;

		if (_mode == CAPTURING && imagePoints.size() > (unsigned)nframes)
		{
			if (runAndSave(_outputFilename, imagePoints, imageSize,
				_boardSize, _pattern, _squareSize, _aspectRatio,
				flags, cameraMatrix, distCoeffs,
				writeExtrinsics, writePoints))
				_mode = CALIBRATED;
			else
				_mode = DETECTION;
		}
	}

	/// show undistorted imgs
	if (showUndistorted) // default: false
	{
		int n = 1;  // Undistort
		string funcName[] = { "Undistort", "Remap" };
		Mat view, rview;
		for (int i = 0; i < (int)imageList.size(); i++) {
			view = imread(imageList[i], 1);
			if (view.empty())	continue;
			if (n == 0)
				undistort(view, rview, cameraMatrix, distCoeffs, cameraMatrix);
			else if (n == 1) {
				Mat map1, map2;
				initUndistortRectifyMap(cameraMatrix, distCoeffs, Mat(),
					getOptimalNewCameraMatrix(cameraMatrix, distCoeffs, imageSize, 1, imageSize, 0),
					imageSize, CV_16SC2, map1, map2);
				remap(view, rview, map1, map2, INTER_LINEAR);
			}
			imshow("Running DR_Calibration" + funcName[n], rview);
			char c = (char)waitKey(300);
			if (c == 27 || c == 'q' || c == 'Q')break;
		}
	}
	cv::destroyAllWindows();
	return true;
}

// Get Method
cv::Mat DR_Calibration::getExtrinsicsBigMat() const
{
	return this->extrinsicsBigMat;
}

cv::Mat DR_Calibration::getCameraMatrix() const
{
	return this->cameraMatrix;
}

cv::Mat DR_Calibration::getDistCoeffsMatrix() const
{
	return this->distCoeffs;
}

vector<int> DR_Calibration::getFoundCheeseBoardVec() const
{
	return this->foundCheeseBoardVec;
}