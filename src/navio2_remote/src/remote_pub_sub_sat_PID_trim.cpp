#include "RCInput.h"
#include "PWM.h"
#include "Util.h"
#include <unistd.h>

#include "ros/ros.h"
#include "sensor_msgs/Temperature.h"
#include "sensor_msgs/Imu.h"
#include <sstream>

#define MOTOR_PWM_OUT 9
#define SERVO_PWM_OUT 0

//Maximum Integration angle
#define MAX_IERR 4

float currentRoll;
ros::Time currentTime;
ros::Time previousTime;

float err;
float derr;
float derr_filt;
float Kierr;

//parameters to pass
float Kp;
float Ki;
float Kd;
float servo_trim;

int pid_Servo_Output(int desired_roll)
{
	//calculate errors
	float previousErr = err;
	ROS_INFO("Error %f", err);
	
	err = desired_roll - currentRoll;
	ROS_INFO("new Err %f", err);
	long timeNow = currentTime.nsec;
	ROS_INFO("Time now %d", timeNow);
	ROS_INFO("prev time %d", previousTime.nsec);
	//time between now and last roll message we got
	double dTnsec = (timeNow - previousTime.nsec);///(10e9f); //in sec
	if(dTnsec < 0) dTnsec += 1e9; // watch out cause its in ns so if it goes beyond 1 sec ...
	double dT = dTnsec/(1e9f);

	ROS_INFO("Dt = %f", dT);
	if(dT > 0)
		derr = (err - previousErr)/dT;
	ROS_INFO("dErr = %f", derr);

	//filtering of derivative
	float tau = 0.025f; //matlab
	float alpha = 0.01f/(0.01f+tau);
	derr_filt = alpha*derr + (1.0f-alpha)*derr_filt;
	ROS_INFO("dErr_filt = %f", derr_filt);

	Kierr += Ki*err*dT;
	ROS_INFO("KiErr = %f", Kierr);
	//old anti wind-up (saturation)
	if(Kierr > MAX_IERR) Kierr = MAX_IERR;
	if(Kierr < -MAX_IERR) Kierr = -MAX_IERR;
	//ROS_INFO("ierr = %f", ierr);
	
	//PID CONTROLLER
	float controlSignal = Kp*err + Kierr + Kd*derr_filt; // should be between +- 22 deg
	
	int pwmSignal = (int)(((-controlSignal*250.0f)/22.0f)+((float)servo_trim));
	if(pwmSignal > 1750) pwmSignal = 1750;
	if(pwmSignal < 1250) pwmSignal = 1250;
	ROS_INFO("control signal %f", controlSignal);
	ROS_INFO("pwm signal %d", pwmSignal);
	return pwmSignal; 
}

void read_Imu(sensor_msgs::Imu imu_msg)
{
	//save the time of the aquisition
	previousTime = currentTime;
	currentTime = imu_msg.header.stamp;
	ROS_INFO("time prev %d time now %d",previousTime.nsec, currentTime.nsec );
	//current roll angle
	currentRoll = imu_msg.orientation.x;
	ROS_INFO("current roll %f", currentRoll);
}

int main(int argc, char **argv)
{
	ROS_INFO("Start");
	int saturation = 2000;
	int freq = 100;
	Kp = 0.6;
	Ki = 2;
	Kd = 0.01;
	servo_trim = 1500;

	ROS_INFO("number of argc %d", argc);

	if(argc == 1)
	{
		//case with default params
	}
	else if(argc == 2)
	{
		//case with frequency
		if(atoi(argv[1]) > 0 )
			freq = atoi(argv[1]);
		else
		{
			ROS_INFO("Frequency must be more than 0");
			return 0;
		}
	}
	else if(argc == 3)
	{
		//case with frequency and saturation
		if(atoi(argv[1]) > 0 )
			freq = atoi(argv[1]);
		else
		{
			ROS_INFO("Frequency must be more than 0");
			return 0;
		}
	
		if(atoi(argv[2]) > 2000) saturation = 2000;
		else saturation = atoi(argv[2]);
	}
	else if(argc == 6)
	{
		//case with frequency and saturation
		if(atoi(argv[1]) > 0 )
			freq = atoi(argv[1]);
		else
		{
			ROS_INFO("Frequency must be more than 0");
			return 0;
		}
	
		if(atoi(argv[2]) > 2000) saturation = 2000;
		else saturation = atoi(argv[2]);

		Kp = atof(argv[3]);
		Ki = atof(argv[4]);
		Kd = atof(argv[5]);
		
	}
	else if(argc == 7)
	{
		//case with frequency and saturation
		if(atoi(argv[1]) > 0 )
			freq = atoi(argv[1]);
		else
		{
			ROS_INFO("Frequency must be more than 0");
			return 0;
		}
	
		if(atoi(argv[2]) > 2000) saturation = 2000;
		else saturation = atoi(argv[2]);

		Kp = atof(argv[3]);
		Ki = atof(argv[4]);
		Kd = atof(argv[5]);

		servo_trim = atoi(argv[6]);
		
	}
	else
	{
		ROS_INFO("not enough arguments ! Specify prbs value and throttle saturation.");
		return 0;
	}

	ROS_INFO("frequency %d, and saturation  : %d, Trim", freq, saturation, servo_trim);


 	/***********************/
	/* Initialize The Node */
	/***********************/
	ros::init(argc, argv, "remote_reading_handler");
	ros::NodeHandle n;
	ros::Publisher remote_pub = n.advertise<sensor_msgs::Temperature>("remote_readings", 1000);
	
	//subscribe to imu topic
	ros::Subscriber imu_sub = n.subscribe("imu_readings", 1000, read_Imu);

	//running rate = freq Hz
	ros::Rate loop_rate(freq);

	/****************************/
	/* Initialize the PID Stuff */
	/****************************/

	currentRoll = 0;
	currentTime = ros::Time::now();
	previousTime = ros::Time::now();
	Kierr = 0;
	err = 0;
	derr = 0;
	derr_filt = 0;
	
	/*******************************************/
	/* Initialize the RC input, and PWM output */
	/*******************************************/

	RCInput rcin;
	rcin.init();
	PWM servo;
	PWM motor;

	if (!motor.init(MOTOR_PWM_OUT)) {
		fprintf(stderr, "Motor Output Enable not set. Are you root?\n");
		return 0;
    	}

	if (!servo.init(SERVO_PWM_OUT)) {
		fprintf(stderr, "Servo Output Enable not set. Are you root?\n");
		return 0;
    	}

	motor.enable(MOTOR_PWM_OUT);
	servo.enable(SERVO_PWM_OUT);

	motor.set_period(MOTOR_PWM_OUT, 50); //frequency 50Hz
	servo.set_period(SERVO_PWM_OUT, 50); 

	int motor_input = 0;
	int servo_input = 0;

	sensor_msgs::Temperature rem_msg;

	float desired_roll = 0;

	while (ros::ok())
	{

		//Throttle saturation
		if(rcin.read(3) >= saturation)
			motor_input = saturation;
		else
			motor_input = rcin.read(3);

		//read desired roll angle with remote ( 1250 to 1750 ) to range of -30 to 30 deg
		desired_roll = -((float)rcin.read(2)-1500.0f)*30.0f/250.0f;
		ROS_INFO("rcin usec = %d    ---   desired roll = %f", rcin.read(2), desired_roll);

		//calculate output to servo from pid controller
		servo_input = pid_Servo_Output(desired_roll);
		
		//write readings on pwm output
		motor.set_duty_cycle(MOTOR_PWM_OUT, ((float)motor_input)/1000.0f); 
		servo.set_duty_cycle(SERVO_PWM_OUT, ((float)servo_input)/1000.0f);
		
		//save values into msg container a
		rem_msg.header.stamp = ros::Time::now();
		rem_msg.temperature = motor_input;
		rem_msg.variance = desired_roll;

		//debug info
		ROS_INFO("Thrust usec = %d    ---   RCinput = %d", motor_input, rcin.read(2));

		//remote_pub.publish(apub);
		remote_pub.publish(rem_msg);
		
		ros::spinOnce();

		loop_rate.sleep();

	}


  	return 0;
}

