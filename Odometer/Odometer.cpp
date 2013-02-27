/**
 * An odometry object for differential drive robots.
 */
#include "Odometer.h"
#include <WheelEncoder.h>
#include <Arduino.h>
#include <Motor.h>

/**
 * You must call setup before using this object to do anything. We need a no-arg
 * constructor so this can be used easily as a member variable.
 */
Odometer::Odometer() {
}

/**
 * Note, we have to know if the encoder runs backwards or forwards.
 */
void Odometer::setup(WheelEncoder* inEncoder[2], boolean isEncoderForward[2], double inWheelDiameter[2], double inTrackWidth) {
	for(int i = 0; i < 2; i++) {
		encoder[i] = inEncoder[i];

		wheelDiameter[i] = inWheelDiameter[i];
		countsPerRevolution[i] = encoder[i]->getCountsPerRevolution();

		distancePerCount[i] = (PI * wheelDiameter[i]) / countsPerRevolution[i];

		if(isEncoderForward[i]) {
			encodeFactor[i] = 1;
		}
		else {
			encodeFactor[i] = -1;
		}
	}

	X = 0.0;
	Y = 0.0;
	heading = 0.0;
	goalX = 0.0;
	goalY = 0.0;
	vLeft = 0.0;
	vRight = 0.0;

	// turnFudgeFactor = 1.0;

	trackWidth = inTrackWidth;

	Serial.print("DistancePerCount[0]:  ");
	Serial.println(distancePerCount[0]);
	Serial.print("trackWidth:  ");
	Serial.println(trackWidth);
	Serial.print("countsPerRevolution[0]:  ");
	Serial.println(countsPerRevolution[0]);

	reset();
}

/**
 * This function resets the encoder counts, only; it does not change the current position.
 */
void Odometer::reset() {
	for(int i = 0; i < 2; i++) {
		encoder[i]->write(0);
	}
	previousLeftEncoderCounts = 0;
	previousRightEncoderCounts = 0;
	previousUpdateTime = millis();
}

void Odometer::setCurrentPosition(double x, double y, double inHeading) {
	reset();
	X = x;
	Y = y;
	heading = inHeading;
}

void Odometer::setGoalPosition(double x, double y) {
	goalX = x;
	goalY = y;
}

/**
 *	Update our position and heading, based on changes in encoder values and the previous heading.
 *	This must be called fairly frequently in order to remain accurate; these equations are
 *	approximations.
 */
void Odometer::update() {
	long leftEncoderCounts = encoder[MOTOR_LEFT]->read() * encodeFactor[MOTOR_LEFT];
	long rightEncoderCounts = encoder[MOTOR_RIGHT]->read() * encodeFactor[MOTOR_RIGHT];

	long deltaLeftTicks = leftEncoderCounts - previousLeftEncoderCounts;
	long deltaRightTicks = rightEncoderCounts - previousRightEncoderCounts;
	previousLeftEncoderCounts = leftEncoderCounts;
	previousRightEncoderCounts = rightEncoderCounts;

	unsigned long currentTime = millis();
	long deltaTime = currentTime - previousUpdateTime;
	previousUpdateTime = currentTime;

	double deltaDistanceLeft = deltaLeftTicks * distancePerCount[MOTOR_LEFT];
	double deltaDistanceRight = deltaRightTicks * distancePerCount[MOTOR_RIGHT];

	double deltaHeading = (double)(deltaDistanceRight - deltaDistanceLeft) / trackWidth ;

	double deltaX, deltaY;
	calculateDeltasCrude(&deltaX, &deltaY, deltaDistanceLeft, deltaDistanceRight);

	if(deltaTime != 0) {
		vLeft = deltaDistanceLeft * 1000.0 / (double)deltaTime;	// cm/s
		vRight = deltaDistanceRight * 1000.0 / (double)deltaTime;
		omega = deltaHeading * 1000.0 / (double)deltaTime; // need this in rad/s
	}

	X += deltaX;
	Y += deltaY;
	heading += deltaHeading;

//	Serial.print("deltaTime = ");
//	Serial.println(deltaTime);
//	Serial.print("deltaDistanceLeft = ");
//	Serial.println(deltaDistanceLeft);
//	Serial.print("deltaDistanceRight = ");
//	Serial.println(deltaDistanceRight);
//	Serial.print("deltaHeading = ");
//	Serial.println(deltaHeading);
//	Serial.print("vLeft = ");
//	Serial.println(vLeft);
//	Serial.print("vRight = ");
//	Serial.println(vRight);
//	Serial.print("omega = ");
//	Serial.println(omega);
//	Serial.println("");

	if(vLeft == 0) {
		Serial.println("Zero left velocity");
	}
	if(vRight == 0) {
		Serial.println("Zero right velocity");
	}

	// limit heading to -Pi <= heading < Pi
	heading = atan2(sin(heading), cos(heading));
		
	// if encoder counts are getting to big, do a reset
	// hand limit is max of int32_t, whatever that value is...
	if((previousLeftEncoderCounts > 32000) || (previousRightEncoderCounts > 32000)) {
		Serial.println("Resetting odometers.");
		reset();
	}
}

void Odometer::calculateDeltasCrude(double *deltaX, double *deltaY,
		double deltaDistanceLeft, double deltaDistanceRight) {
	double deltaDistance = 0.5 * (deltaDistanceLeft + deltaDistanceRight);

	*deltaX = deltaDistance * cos(heading);
	*deltaY = deltaDistance * sin(heading);
}

/**
 * See http://rossum.sourceforge.net/papers/DiffSteer/
 */
void Odometer::calculateDeltasRefined(double *deltaX, double *deltaY,
		double deltaDistanceLeft, double deltaDistanceRight) {
	double deltaDistance = 0.5 * (deltaDistanceLeft + deltaDistanceRight);
	double deltaDiff = deltaDistanceRight - deltaDistanceLeft;

	// we are assuming constant velocity for each wheel (though they may differ from each other)
	*deltaX = ((trackWidth * deltaDistance) / (2 * deltaDiff)) *
			(sin((deltaDistance / trackWidth) + heading) - sin(heading));
	*deltaY = - ((trackWidth * deltaDistance) / (2 * deltaDiff)) *
			(cos((deltaDistance / trackWidth) + heading) - cos(heading));
}

/**
 * Get the velocity of the center of the robot. This may be negative.
 */
double Odometer::getLinearVelocity() {
	return (vLeft + vRight) / 2.0;
}

/**
 *	This translates an angular + linear velocity into normalized left and right components.
 *
 *	Commanded linear and angular velocities are assumed to be -1 to 1. This does NOT use PID.
 */
void Odometer::translateToLeftRightVelocities(float* normLeft, float* normRight, float normedLinearVelocity, float normedLinearAngularVelocity) {
	float leftVelocity = normedLinearVelocity;
	float rightVelocity = normedLinearVelocity;

	// apply correction for angular. +1 is all the way left, -1 all the way right
	leftVelocity -= normedLinearAngularVelocity;
	rightVelocity += normedLinearAngularVelocity;

	float NormalizedLeftCV = constrain(leftVelocity, -1, +1);
	float NormalizedRightCV = constrain(rightVelocity, -1, +1);

	*normLeft = NormalizedLeftCV;
	*normRight = NormalizedRightCV;
}

/**
 * What heading (in radians) will steer us towards our goal?
 *
 */
double Odometer::calculateGoalHeading() {
	 double calcHeading = atan2(goalY - Y, goalX - X);
	 return calcHeading;
}

/**
 * 	This returns a normalized error factor (between -1 and 1) representing how we are from the
 * 	correct heading. Positive value means we need to turn clockwise, i.e. make the left side
 * 	faster. This can be used for PID control or other corrections.
 *
 * 	When called with no argument, we calculate out the right heading to reach to
 * 	reach the goal POINT. With a radian argument, we attempt to match that heading.
 */
double Odometer::getNormalizedHeadingError() {
	double requiredHeading = calculateGoalHeading();
	return getNormalizedHeadingError(requiredHeading);
}

/**
 * 	This returns a normalized error factor (between -1 and 1) representing how we are from the
 * 	correct heading. Positive value means we need to turn clockwise, i.e. make the left side
 * 	faster. This can be used for PID control or other corrections.
 *
 * 	When called with no argument, we calculate out the right heading to reach to
 * 	reach the goal POINT. With a radian argument, we attempt to match that heading.
 */
double Odometer::getNormalizedHeadingError(double requiredHeading) {
	double headingDiff = heading - requiredHeading;
//	Serial.print("Heading = ");
//	Serial.println(heading);
//	Serial.print("Required heading = ");
//	Serial.println(requiredHeading);

	// for safety, use atan2 which guarantees -PI to PI range
	double error = atan2(sin(headingDiff), cos(headingDiff)) / PI;

//	Serial.print("Error = ");
//	Serial.println(error);
	return error;
}

double Odometer::getDistanceToGoal() {
//	Serial.print("x distance: ");
//	Serial.println(GoalX - X);
//	Serial.print("y distance: ");
//	Serial.println(GoalY - Y);

	return sqrt(pow(goalX - X, 2) + pow(goalY - Y, 2));
}
