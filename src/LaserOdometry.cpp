#include <slam3d/LaserOdometry.hpp>

#include <utility>
#include <algorithm>
#include <iostream>

#include <boost/make_shared.hpp>
#include <Eigen/Dense>

// [LOAM] Zhang, J., & Singh, S. LOAM : Lidar Odometry and Mapping in Real-time.

using namespace slam3d;

typedef std::vector< std::pair<double, unsigned int> > ValueList;

LaserOdometry::LaserOdometry()
{
	mMaxSurfaceAngleDeg = 20;
	mLaserAngleDeg = 0.25;
	
	double sin1 = sin(DEG2RAD(mLaserAngleDeg));
	double sin2 = sin(DEG2RAD(mMaxSurfaceAngleDeg));
	mDistanceRelation = (sin1 * sin1) / (sin2 * sin2);
	
	mScanSize = -1;
	
	for(int i = 0; i < 6; i++)
	{
		transform[i] = 0;
		transformRec[i] = 0;
		transformSum[i] = 0;	
	}
}

LaserOdometry::~LaserOdometry()
{
	
}

void LaserOdometry::addScan(PointCloud::ConstPtr scan)
{
	// Set scan size
	if(mScanSize < 0)
		mScanSize = scan->size();
	
	mEdgePoints.header = scan->header;
	mSurfacePoints.header = scan->header;
	mExtraPoints.header = scan->header;

//	st = (timeLasted - startTime) / (startTimeCur - startTimeLast);
	mRelativeSweepTime = (scan->header.stamp - mCurrentSweepStart) / (mCurrentSweepStart - mLastSweepStart);

	extractFeatures(scan);
	calculatePose();
	
	mLastScanTime = scan->header.stamp;
}

void LaserOdometry::extractFeatures(PointCloud::ConstPtr scan)
{
	// Points flagged in this array are filtered from being used as features
	unsigned int cloudSize = scan->points.size();
	int filter[cloudSize];
	memset(filter, 0, sizeof(filter));

	for (int i = 5; i < cloudSize - 6; i++)
	{
		float diffX = scan->points[i + 1].x - scan->points[i].x;
		float diffY = scan->points[i + 1].y - scan->points[i].y;
		float diffZ = scan->points[i + 1].z - scan->points[i].z;
		float diff = diffX * diffX + diffY * diffY + diffZ * diffZ;

		float depth1 = sqrt(scan->points[i].x * scan->points[i].x + 
			scan->points[i].y * scan->points[i].y +
			scan->points[i].z * scan->points[i].z);

		// Filter points on boundaries of occluded regions
		if (diff > 0.05)
		{
			float depth2 = sqrt(scan->points[i + 1].x * scan->points[i + 1].x + 
				scan->points[i + 1].y * scan->points[i + 1].y +
				scan->points[i + 1].z * scan->points[i + 1].z);

			if (depth1 > depth2)
			{
				diffX = scan->points[i + 1].x - scan->points[i].x * depth2 / depth1;
				diffY = scan->points[i + 1].y - scan->points[i].y * depth2 / depth1;
				diffZ = scan->points[i + 1].z - scan->points[i].z * depth2 / depth1;

				if (sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ) / depth2 < 0.1)
				{
					filter[i - 5] = 1;
					filter[i - 4] = 1;
					filter[i - 3] = 1;
					filter[i - 2] = 1;
					filter[i - 1] = 1;
					filter[i] = 1;
				}
			} else
			{
				diffX = scan->points[i + 1].x * depth1 / depth2 - scan->points[i].x;
				diffY = scan->points[i + 1].y * depth1 / depth2 - scan->points[i].y;
				diffZ = scan->points[i + 1].z * depth1 / depth2 - scan->points[i].z;

				if (sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ) / depth1 < 0.1)
				{
					filter[i + 1] = 1;
					filter[i + 2] = 1;
					filter[i + 3] = 1;
					filter[i + 4] = 1;
					filter[i + 5] = 1;
					filter[i + 6] = 1;
				}
			}
		}

		// Filter points that are raughly parallel using law of sines
		float diffX2 = scan->points[i].x - scan->points[i - 1].x;
		float diffY2 = scan->points[i].y - scan->points[i - 1].y;
		float diffZ2 = scan->points[i].z - scan->points[i - 1].z;
		float diff2 = diffX2 * diffX2 + diffY2 * diffY2 + diffZ2 * diffZ2;

		if (diff > mDistanceRelation * depth1 && diff2 > mDistanceRelation * depth1)
		{
			filter[i] = 1;
		}
	}

	// Calculate c-Values [LOAM: V-A (1)]
	ValueList c_values[4];
	unsigned int sectionSize = (cloudSize - 10) / 4.0;
	unsigned int i = 5;
	for(unsigned int section = 0; section < 4; section++)
	{
		for(unsigned int c = 0; c < sectionSize; c++, i++)
		{
			// TODO: Multiply with defined kernel instead of this...
			double diffX = scan->points[i - 5].x + scan->points[i - 4].x 
				+ scan->points[i - 3].x + scan->points[i - 2].x 
				+ scan->points[i - 1].x - 10 * scan->points[i].x 
				+ scan->points[i + 1].x + scan->points[i + 2].x
				+ scan->points[i + 3].x + scan->points[i + 4].x
				+ scan->points[i + 5].x;
			double diffY = scan->points[i - 5].y + scan->points[i - 4].y 
				+ scan->points[i - 3].y + scan->points[i - 2].y 
				+ scan->points[i - 1].y - 10 * scan->points[i].y 
				+ scan->points[i + 1].y + scan->points[i + 2].y
				+ scan->points[i + 3].y + scan->points[i + 4].y
				+ scan->points[i + 5].y;
			double diffZ = scan->points[i - 5].z + scan->points[i - 4].z 
				+ scan->points[i - 3].z + scan->points[i - 2].z 
				+ scan->points[i - 1].z - 10 * scan->points[i].z 
				+ scan->points[i + 1].z + scan->points[i + 2].z
				+ scan->points[i + 3].z + scan->points[i + 4].z
				+ scan->points[i + 5].z;

			c_values[section].push_back(std::make_pair(diffX * diffX + diffY * diffY + diffZ * diffZ, i));
		}

		// Sort the points based on their c-value
		std::sort(c_values[section].begin(), c_values[section].end());
	
		// Select the points with largest c-Value (edges)
		int largestPickedNum = 0;
		for (ValueList::reverse_iterator i = c_values[section].rbegin(); i != c_values[section].rend(); i++)
		{
			if (filter[i->second] == 0 && i->first > 0.1)
			{
				PointType newFeature = scan->points[i->second];
				newFeature.intensity = scan->header.stamp;
				largestPickedNum++;
				if (largestPickedNum <= 2)
				{
					mEdgePoints.push_back(newFeature);
				} else if (largestPickedNum <= 20)
				{
					mExtraPoints.push_back(scan->points[i->second]);
				}else
				{
					break;
				}
				
				// Invalidate points nearby
				for (int k = i->second-5; k <= i->second+5; k++)
				{
					float diffX = scan->points[k].x - scan->points[i->second].x;
					float diffY = scan->points[k].y - scan->points[i->second].y;
					float diffZ = scan->points[k].z - scan->points[i->second].z;
					if (diffX * diffX + diffY * diffY + diffZ * diffZ <= 0.2)
					{
						filter[k] = 1;
					}
				}
			}
		}
	
		// Select the points with smallest c-Value (surfaces)
		int smallestPickedNum = 0;
		for (ValueList::iterator i = c_values[section].begin(); i != c_values[section].end(); i++)
		{
			if (filter[i->second] == 0 && i->first < 0.1)
			{
				PointType newFeature = scan->points[i->second];
				newFeature.intensity = scan->header.stamp;
				smallestPickedNum++;
				if (smallestPickedNum <= 4)
				{
					mSurfacePoints.push_back(newFeature);
				}else
				{
					mExtraPoints.push_back(newFeature);
				}
					
				// Invalidate points nearby
				for (int k = i->second-5; k <= i->second+5; k++)
				{
					float diffX = scan->points[k].x - scan->points[i->second].x;
					float diffY = scan->points[k].y - scan->points[i->second].y;
					float diffZ = scan->points[k].z - scan->points[i->second].z;
					if (diffX * diffX + diffY * diffY + diffZ * diffZ <= 0.2)
					{
						filter[k] = 1;
					}
				}
			}
		}
	}
}

void LaserOdometry::calculatePose()
{
	if(mLastEdgePoints.size() == 0)
		return;
		
	for(int i = 0; i < 25; i++)
	{
		if(doNonlinearOptimization(i))
			break;
	}
}

bool LaserOdometry::doNonlinearOptimization(int iteration)
{	
	// Some definitions from LOAM
	PointCloud laserCloudExtreOri;
	PointCloud coeffSel;
	
	// Correspondences for edge points
	std::vector<int> pointSearchInd;
	std::vector<float> pointSearchSqDis;
		
	for(PointCloud::iterator point_i = mEdgePoints.begin(); point_i < mEdgePoints.end(); point_i++)
	{
		// Transform point to ??? time
		PointType point_i_sh = *point_i; // timeShift(*point_i, timestamp);
		
		// Let j be the nearest neighbor of i within the previous sweep
		mEdgeTree.nearestKSearch(point_i_sh, 1, pointSearchInd, pointSearchSqDis);
		int index_j = pointSearchInd[0];
		double time_j = mLastEdgePoints[index_j].intensity;
		
		if (pointSearchSqDis[0] > 1.0)
			continue;
		PointCloud::iterator point_j = mLastEdgePoints.begin() + index_j;

		// Let l be the nearest neighbor of i within an adjacent scan
		int index_l = 1;
		double min_dis_l = 1;
		
		int begin = std::max(0, index_j - (2*mScanSize));
		int end = std::min((int)mLastEdgePoints.size(), index_j + (2*mScanSize));
		for(int l = begin; l < end; l++)
		{
			// Check if it is an adjacent scan via the timestamp (-_-)
			if (!((mLastEdgePoints[l].intensity < time_j - 0.005 && mLastEdgePoints[l].intensity > time_j - 0.07) || 
			      (mLastEdgePoints[l].intensity > time_j + 0.005 && mLastEdgePoints[l].intensity < time_j + 0.07)))
				continue;

			// Calculate distance between Points i and l (why?)
			double sq_dis_i_l = (mLastEdgePoints[l].x - point_i_sh.x) * (mLastEdgePoints[l].x - point_i_sh.x) +
			             (mLastEdgePoints[l].x - point_i_sh.y) * (mLastEdgePoints[l].x - point_i_sh.y) + 
			             (mLastEdgePoints[l].x - point_i_sh.z) * (mLastEdgePoints[l].x - point_i_sh.z);

			if (sq_dis_i_l < min_dis_l)
			{
				min_dis_l = sq_dis_i_l;
				index_l = l;
			}
		}

		// Calculate distance of i to line (j,l)
		if (index_l >= 0)
		{
			PointType tripod1 = mLastEdgePoints[index_j];
			PointType tripod2 = mLastEdgePoints[index_l];

			float x0 = point_i_sh.x;
			float y0 = point_i_sh.y;
			float z0 = point_i_sh.z;
			float x1 = tripod1.x;
			float y1 = tripod1.y;
			float z1 = tripod1.z;
			float x2 = tripod2.x;
			float y2 = tripod2.y;
			float z2 = tripod2.z;

			float a012 = sqrt(((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1))
				* ((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
				+ ((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1))
				* ((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1)) 
				+ ((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1))
				* ((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1)));

			float l12 = sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2) + (z1 - z2)*(z1 - z2));

			float la = ((y1 - y2)*((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
			+ (z1 - z2)*((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1))) / a012 / l12;

			float lb = -((x1 - x2)*((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
			- (z1 - z2)*((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1))) / a012 / l12;

			float lc = -((x1 - x2)*((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1)) 
			+ (y1 - y2)*((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1))) / a012 / l12;

			float ld2 = a012 / l12;

			PointType point_i_proj = point_i_sh;
			point_i_proj.x -= la * ld2;
			point_i_proj.y -= lb * ld2;
			point_i_proj.z -= lc * ld2;

			// What is "s" ???
			float s = 2 * (1 - 8 * fabs(ld2));

			PointType coeff;
			coeff.x = s * la;
			coeff.y = s * lb;
			coeff.z = s * lc;
			coeff.intensity = s * ld2;

			if (s > 0.4)
			{
				laserCloudExtreOri.push_back(*point_i);
				coeffSel.push_back(coeff);
			}
		}
	}

	// Correspondences for surface points
	// TODO
	
	
	// laserCloudExtreOri: Feature points from current sweep, that have 
	//                     correspondences in the last sweep
	// coeffSel: ???
	
	// Calculate the motion between current scan and last sweep
	int extrePointSelNum = laserCloudExtreOri.points.size();
	std::cout << "PointCloud laserCloudExtreOri has " << extrePointSelNum << " Points." << std::endl;
	if (extrePointSelNum < 10)
	{
		return false;
	}
	
	// Levenberg-Marquardt-Algorithm
	Eigen::Matrix<float, Eigen::Dynamic, 6> matA(extrePointSelNum, 6);
	Eigen::Matrix<float, Eigen::Dynamic, 6> matAt(extrePointSelNum, 6);
	Eigen::Matrix<float, 6, 6> matAtA;
	Eigen::Matrix<float, Eigen::Dynamic, 1> matB(extrePointSelNum, 1);
	Eigen::Matrix<float, 6, 1> matAtB;
	Eigen::Matrix<float, 6, 1> matX;
	
	for (int i = 0; i < extrePointSelNum; i++)
	{
		PointType extreOri = laserCloudExtreOri[i];
		PointType coeff = coeffSel[i];

		// Scan time / Sweep time
//		float s = (timeLasted - timeLastedRec) / (startTimeCur - startTimeLast);
		float s =  (extreOri.intensity - mLastScanTime) / (mCurrentSweepStart- mLastSweepStart);

		float srx = sin(s * transform[0]);
		float crx = cos(s * transform[0]);
		float sry = sin(s * transform[1]);
		float cry = cos(s * transform[1]);
		float srz = sin(s * transform[2]);
		float crz = cos(s * transform[2]);
		float tx = s * transform[3];
		float ty = s * transform[4];
		float tz = s * transform[5];

		float arx = (-s*crx*sry*srz*extreOri.x + s*crx*crz*sry*extreOri.y + s*srx*sry*extreOri.z 
		+ s*tx*crx*sry*srz - s*ty*crx*crz*sry - s*tz*srx*sry) * coeff.x
		+ (s*srx*srz*extreOri.x - s*crz*srx*extreOri.y + s*crx*extreOri.z
		+ s*ty*crz*srx - s*tz*crx - s*tx*srx*srz) * coeff.y
		+ (s*crx*cry*srz*extreOri.x - s*crx*cry*crz*extreOri.y - s*cry*srx*extreOri.z
		+ s*tz*cry*srx + s*ty*crx*cry*crz - s*tx*crx*cry*srz) * coeff.z;

		float ary = ((-s*crz*sry - s*cry*srx*srz)*extreOri.x 
		+ (s*cry*crz*srx - s*sry*srz)*extreOri.y - s*crx*cry*extreOri.z 
		+ tx*(s*crz*sry + s*cry*srx*srz) + ty*(s*sry*srz - s*cry*crz*srx) 
		+ s*tz*crx*cry) * coeff.x
		+ ((s*cry*crz - s*srx*sry*srz)*extreOri.x 
		+ (s*cry*srz + s*crz*srx*sry)*extreOri.y - s*crx*sry*extreOri.z
		+ s*tz*crx*sry - ty*(s*cry*srz + s*crz*srx*sry) 
		- tx*(s*cry*crz - s*srx*sry*srz)) * coeff.z;

		float arz = ((-s*cry*srz - s*crz*srx*sry)*extreOri.x + (s*cry*crz - s*srx*sry*srz)*extreOri.y
		+ tx*(s*cry*srz + s*crz*srx*sry) - ty*(s*cry*crz - s*srx*sry*srz)) * coeff.x
		+ (-s*crx*crz*extreOri.x - s*crx*srz*extreOri.y
		+ s*ty*crx*srz + s*tx*crx*crz) * coeff.y
		+ ((s*cry*crz*srx - s*sry*srz)*extreOri.x + (s*crz*sry + s*cry*srx*srz)*extreOri.y
		+ tx*(s*sry*srz - s*cry*crz*srx) - ty*(s*crz*sry + s*cry*srx*srz)) * coeff.z;

		float atx = -s*(cry*crz - srx*sry*srz) * coeff.x + s*crx*srz * coeff.y 
		- s*(crz*sry + cry*srx*srz) * coeff.z;

		float aty = -s*(cry*srz + crz*srx*sry) * coeff.x - s*crx*crz * coeff.y 
		- s*(sry*srz - cry*crz*srx) * coeff.z;

		float atz = s*crx*sry * coeff.x - s*srx * coeff.y - s*crx*cry * coeff.z;

		float d2 = coeff.intensity;

		matA(i, 0) = arx;
		matA(i, 1) = ary;
		matA(i, 2) = arz;
		matA(i, 3) = atx;
		matA(i, 4) = aty;
		matA(i, 5) = atz;
		matB(i, 0) = -0.015 * mRelativeSweepTime * d2;
	}
//	cv::transpose(matA, matAt);
//	matAtA = matAt * matA; //+ 0.1 * cv::Mat::eye(6, 6, CV_32F);
//	matAtB = matAt * matB;
//	cv::solve(matAtA, matAtB, matX, cv::DECOMP_QR);
	
	matAt = matA.transpose();
	matAtA = matAt * matA;
	matAtB = matAt * matB;
	matX = matAtA.colPivHouseholderQr().solve(matAtB);

	if (fabs(matX(0, 0)) < 0.005 &&
		fabs(matX(1, 0)) < 0.005 &&
		fabs(matX(2, 0)) < 0.005 &&
		fabs(matX(3, 0)) < 0.01 &&
		fabs(matX(4, 0)) < 0.01 &&
		fabs(matX(5, 0)) < 0.01)
	{
		transform[0] += 0.1 * matX(0, 0);
		transform[1] += 0.1 * matX(1, 0);
		transform[2] += 0.1 * matX(2, 0);
		transform[3] += matX(3, 0);
		transform[4] += matX(4, 0);
		transform[5] += matX(5, 0);
	}else
	{
		std::cout << "Odometry update out of bound!" << std::endl;
	}

	float deltaR = sqrt(RAD2DEG(matX(0, 0)) * RAD2DEG(matX(0, 0))
	                  + RAD2DEG(matX(1, 0)) * RAD2DEG(matX(1, 0))
	                  + RAD2DEG(matX(2, 0)) * RAD2DEG(matX(2, 0)));
	float deltaT = sqrt(matX(3, 0) * 100 * matX(3, 0) * 100
	                  + matX(4, 0) * 100 * matX(4, 0) * 100
	                  + matX(5, 0) * 100 * matX(5, 0) * 100);

	if (deltaR < 0.02 && deltaT < 0.02)
	{
		return true;
	}
	return false;
}

void LaserOdometry::finishSweep(double timestamp)
{
	mLastSweep = mEdgePoints;
	mLastSweep += mSurfacePoints;
	mLastSweep += mExtraPoints;
	
	mLastEdgePoints = mEdgePoints;
	mLastSurfacePoints = mSurfacePoints;
	
	mEdgeTree.setInputCloud(boost::make_shared<PointCloud>(mLastEdgePoints));
	mSurfaceTree.setInputCloud(boost::make_shared<PointCloud>(mLastSurfacePoints));
	
	// Shouldn't this be done before setting the kdTree?
	timeShift(mLastEdgePoints, timestamp);
	timeShift(mLastSurfacePoints, timestamp);
	
	mEdgePoints.clear();
	mSurfacePoints.clear();
	mExtraPoints.clear();
	
	mLastSweepStart = mCurrentSweepStart;
	mCurrentSweepStart = timestamp;
}

void LaserOdometry::timeShift(PointCloud& pointcloud, double timestamp)
{
	
}