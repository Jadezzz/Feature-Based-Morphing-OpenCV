#include <iostream>
#include <vector>
#include <math.h>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

// Window position
#define WINDOW_X 100
#define WINDOW_Y 200
#define PADDING 20

// Animation
#define DELAY 1000
#define FRAME_COUNT 10

// Parameters for weight calculation
#define PARAM_A 1
#define PARAM_B 2
#define PARAM_P 2

// Data Structures
class FeatureLine {
public:
    Point2d P; // Start point
    Point2d Q; // End point
    Point2d M; // Middle point
    double length;
    double angle;
    
    FeatureLine() {
        return;
    }
    
    FeatureLine(Point2d start, Point2d end) {
        // Assign start and end points
        P = start;
        Q = end;
        M = (P + Q) / 2;
        
        Point2d diff = Q - P;
        
        // Compute length
        length = norm(diff);
        
        // Compute angle
        angle = atan2(diff.y, diff.x);
    }
    
    FeatureLine(Point2d middle, double length, double angle) {
        // Restore a line from middle, length and angle
        double deltaX = length / 2 * cos(angle);
        double deltaY = length / 2 * sin(angle);
        
        P = Point2d((middle.x - deltaX), (middle.y - deltaY));
        Q = Point2d((middle.x + deltaX), (middle.y + deltaY));
        
        M = middle;
        this->length = length;
        this->angle = angle;
    }
    
    Point2d computePerpendicular() {
        // Retrun a perpendicular vector
        Point2d QP = Q - P;
        return Point2d(QP.y, -QP.x);
    }
    
    double computeU(Point2d X) {
        // Compute u
        return (X - P).dot(Q - P) / (length * length);
    }
    
    double computeV(Point2d X) {
        // Compute v
        return (X - P).dot(computePerpendicular()) / length;
    }
    
    Point2d computePoint(double u, double v) {
        // Compute point respect to line from u and v
        return P + u * (Q - P) + v * computePerpendicular() / length;
    }
    
    double computeWeight(Point2d X) {
        // Compute point weight with respect to line
        double u = computeU(X);
        double dist;
        if (u > 1.0) {
            // If u > 1, dist is X-Q
            dist = norm(X - Q);
        } else if (u < 0) {
            // If u < 0, dist is X-P
            dist = norm(X - P);
        } else {
            // If 0 < u < 1, dist is abs(v)
            dist = abs(computeV(X));
        }
        
        return pow((pow(length, PARAM_P) / (PARAM_A + dist)), PARAM_B);
    }
    
};

class FeatureLinePair {
public:
    FeatureLine source;
    FeatureLine dest;
    
    FeatureLinePair(FeatureLine source, FeatureLine dest) {
        this->source = source;
        this->dest = dest;
    }
    
    FeatureLine interpolateLine(double alpha) {
        // Interpolate feature line between source and dest using ratio (alpha)
        while (source.angle - dest.angle > M_PI) {
            dest.angle += M_PI;
        }
        while (dest.angle - source.angle > M_PI) {
            source.angle += M_PI;
        }
        
        Point2d M = (1 - alpha) * source.M + alpha * dest.M;
        double length = (1 - alpha) * source.length + alpha * dest.length;
        double angle = (1 - alpha) * source.angle + alpha * dest.angle;
        
        return FeatureLine(M, length, angle);
    }
};

// Image to display
Mat showImageSource;
Mat showImageDest;

// Window states
Point2d winSourceStart;
Point2d winSourceEnd;
Point2d winDestStart;
Point2d winDestEnd;
bool winSourceDrag = false;
bool winDestDrag = false;
bool winSourceActive = false;
bool winDestActive = false;

// Clip a point inside boundaries
Point2d clipPoint(Point2d p, int rows, int cols);
// Bilinear interpolate the color based on point p in img
Vec3b bilinearColor(Mat img, Point2d p);
// Return two corresponding points, the first one is the point warp to source image, the second one is the point warp to dest image
vector<Point2d> warpPoint(Point2d p, vector<FeatureLinePair> featureLinePairs, double alpha);
// Warp an image
Mat warpImage(Mat source, Mat dest, vector<FeatureLinePair> featureLinePairs, double alpha);

// Mouse callbacks
void onMouseImageSource(int event, int x, int y, int flags, void* userdata);
void onMouseImageDest(int event, int x, int y, int flags, void* userdata);

// Global variables
vector<FeatureLinePair> featureLinePairs;
FeatureLine curSourceLine;
FeatureLine curDestLine;

int main(int argc, char* argv[]) {
    // Check arguments
    if (argc != 3) {
        cout << "Must provide 2 image paths as arguments to proceed!" << endl;
        return -1;
    }
    // Read images
    Mat imageSource, imageDest;
    imageSource = imread(argv[1]);
    imageDest = imread(argv[2]);
    
    if (!imageSource.data || !imageDest.data) {
        cout << "Count not open or find image!" << endl;
        return -1;
    }
    // Check image dimensions
    if (!(imageSource.rows == imageDest.rows && imageSource.cols == imageDest.cols)) {
        // If the size do not match, resize destination image
        cv::resize(imageDest, imageDest, imageSource.size());
    }
    
    // Create images for showing
    showImageSource = imageSource.clone();
    showImageDest = imageDest.clone();
    
    // Create windows
    namedWindow("Source Image");
    namedWindow("Destination Image");
    // Move images side-by-side
    moveWindow("Source Image", WINDOW_X, WINDOW_Y);
    moveWindow("Destination Image", WINDOW_X + PADDING + imageSource.cols, WINDOW_Y);
    // Show images
    imshow("Source Image", showImageSource);
    imshow("Destination Image", showImageDest);
    
    setMouseCallback("Source Image", onMouseImageSource);
    setMouseCallback("Destination Image", onMouseImageDest);
    
    cout << "Usage:" << endl;
    cout << "Press 'a' to add new pair of feature lines" << endl;
    cout << "Press 's' to start warping" << endl;
    cout << "Press ESC/'q' to quit" << endl;
    // Main Loop
    while (true) {
        // Get key's code
        int key = waitKey(0);
        
        // ESC is pressed, exit
        if (key == 27 || key == 113) {
            break;
        }
        
        // a is pressed, start drawing pairs
        else if (key == 97) {
            winSourceActive = true;
        }
        
        // s is pressed, start processing
        else if (key == 115) {
            vector<Mat> resultImages;
            // Warping
            cout << "Computing";
            for (int i = 0; i <= FRAME_COUNT; i++) {
                double ratio = (double) i / FRAME_COUNT;
                resultImages.push_back(warpImage(imageSource, imageDest, featureLinePairs, ratio));
                cout << ".";
            }
            cout << "Complete!" << endl;
            
            namedWindow("Result Image");
            moveWindow("Result Image", WINDOW_X + 2 * imageSource.cols + 2 * PADDING, WINDOW_Y);
            for (int i = 0; i < resultImages.size(); i++) {
                imshow("Result Image", resultImages[i]);
                waitKey(DELAY);
            }
        }
        
    }
    return 0;
}

void onMouseImageSource(int event, int x, int y, int flags, void* userdata) {
    if (winSourceActive) {
        if (event == EVENT_LBUTTONDOWN) {
            // Set drag state
            winSourceDrag = true;
            // Record start point
            winSourceStart = Point2d(x, y);
        } else if (event == EVENT_LBUTTONUP) {
            // Unset drag state
            winSourceDrag = false;
            // Record end point
            winSourceEnd = Point2d(x, y);
            
            // Set window1 inactive, window2 active
            winSourceActive = false;
            winDestActive = true;
            
            // Draw on image
            line(showImageSource, winSourceStart, winSourceEnd, Scalar(0,255,0), 2);
            imshow("Source Image", showImageSource);
            
            curSourceLine = FeatureLine(winSourceStart, winSourceEnd);
            
            
        } else if (event == EVENT_MOUSEMOVE) {
            if (winSourceDrag) {
                // Create temporary image to show expected result
                Mat tempImage = showImageSource.clone();
                line(tempImage, winSourceStart, Point2d(x, y), Scalar(0,255,0), 2);
                imshow("Source Image", tempImage);
            }
        }
    }
    
}

void onMouseImageDest(int event, int x, int y, int flags, void* userdata) {
    if (winDestActive) {
        if (event == EVENT_LBUTTONDOWN) {
            // Set drag state
            winDestDrag = true;
            // Record start point
            winDestStart = Point2d(x, y);
        } else if (event == EVENT_LBUTTONUP) {
            // Unset drag state
            winDestDrag = false;
            // Record end point
            winDestEnd = Point2d(x, y);
            
            // Set both window inactive
            winDestActive = false;
            
            // Draw on image
            line(showImageDest, winDestStart, winDestEnd, Scalar(0,255,0), 2);
            imshow("Destination Image", showImageDest);
            
            curDestLine = FeatureLine(winDestStart, winDestEnd);
            
            featureLinePairs.push_back(FeatureLinePair(curSourceLine, curDestLine));
            
        } else if (event == EVENT_MOUSEMOVE) {
            if (winDestDrag) {
                // Create temporary image to show expected result
                Mat tempImage = showImageDest.clone();
                line(tempImage, winDestStart, Point2d(x, y), Scalar(0,255,0), 2);
                imshow("Destination Image", tempImage);
            }
        }
    }
}

Point2d clipPoint(Point2d p, int rows, int cols) {
    Point2d out = Point2d(p);
    if (out.x < 0)
        out.x = 0;
    else if (out.x > cols - 1)
        out.x = cols - 1;
    if (out.y < 0)
        out.y = 0;
    else if (out.y > rows - 1)
        out.y = rows - 1;
    return out;
}

Vec3b bilinearColor(Mat img, Point2d p) {
    int x_floor = floor(p.x);
    int y_floor = floor(p.y);
    int x_ceil = ceil(p.x);
    int y_ceil = ceil(p.y);
    
    double u = p.x - x_floor;
    double v = p.y - y_floor;
    
    Vec3b out = Vec3b(0, 0, 0);
    
    Vec3d topLeft = img.at<Vec3b>(y_floor, x_floor);
    Vec3d topRight = img.at<Vec3b>(y_floor, x_ceil);
    Vec3d bottomLeft = img.at<Vec3b>(y_ceil, x_floor);
    Vec3d bottomRight = img.at<Vec3b>(y_ceil, x_ceil);
    
    // Bilinear interpolation
    out = (1 - v) * ((1 - u) * topLeft + u * topRight)
    + v * ((1 - u) * bottomLeft + u * bottomRight);
    
    return out;
}

vector<Point2d> warpPoint(Point2d p, vector<FeatureLinePair> featureLinePairs, double alpha) {
/*
 Warp a point to source and destination image
 
 Psuedocode:
 
 for point p do
    psum = (0, 0)
    wsum = 0
    foreach line L[i] in destination do
        p_[i] = p transformed by (L[i], L_[i])
        psum = psum + p_[i] * weight[i]
        wsum += weight[i]
    end
 */
    vector<Point2d> out;
    Point2d p_source_sum = Point2d(0, 0);
    Point2d p_dest_sum = Point2d(0, 0);
    
    double w_source_sum = 0;
    double w_dest_sum = 0;
    
    for (int i = 0; i < featureLinePairs.size(); i++) {
        FeatureLine sourceLine = featureLinePairs[i].source;
        FeatureLine middleLine = featureLinePairs[i].interpolateLine(alpha);
        FeatureLine destLine = featureLinePairs[i].dest;
        
        // Transfomr by line
        Point2d p_source = sourceLine.computePoint(middleLine.computeU(p), middleLine.computeV(p));
        Point2d p_dest = destLine.computePoint(middleLine.computeU(p), middleLine.computeV(p));
        
        // Compute weight
        double w_source = sourceLine.computeWeight(p_source);
        double w_dest = destLine.computeWeight(p_dest);
        
        // Update p and w
        p_source_sum += p_source * w_source;
        w_source_sum += w_source;
        
        p_dest_sum += p_dest * w_dest;
        w_dest_sum += w_dest;
    }
    
    Point2d p_src = p_source_sum / w_source_sum;
    Point2d p_dest = p_dest_sum / w_dest_sum;
    
    out.push_back(p_src);
    out.push_back(p_dest);
    return out;
}

Mat warpImage(Mat source, Mat dest, vector<FeatureLinePair> featureLinePairs, double alpha) {

/*
 Warp an image
 
 Psuedocode
 for each desitination pixel p do
    p_src, p_dest = warpPoint(p)
    clipPoint(p_src)
    clipPoint(p_dest)
    Result(p) = (1 - alpha) * bilinearColor(p_src) + alpha * bilinearColor(p_dest)
 end
 */
    Mat sourceImg = source.clone();
    Mat destImg = dest.clone();
    
    // Generate output image
    Mat out = Mat(sourceImg.size(), CV_8UC3);
    
    // Foreach destination pixels p
    for (int i = 0; i < out.cols; i++) {
        for (int j = 0; j < out.rows; j++) {
            Point2d p = Point2d(i, j);
            vector<Point2d> points = warpPoint(p, featureLinePairs, alpha);
            Point2d p_src = clipPoint(points[0], out.rows, out.cols );
            Point2d p_dest = clipPoint(points[1], out.rows, out.cols);
            
            Vec3d color_src = bilinearColor(sourceImg, p_src);
            Vec3d color_dest = bilinearColor(destImg, p_dest);
            
            Vec3b color = (1 - alpha) * color_src + alpha * color_dest;
            out.at<Vec3b>(j, i) = color;
        }
    }
    
    return out;
}
