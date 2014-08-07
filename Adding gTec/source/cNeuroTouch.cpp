/*
 * File:   cNeuroTouch.cpp
 * Author: Robert Romano
 * Edited by: Darrel R. Deo
 *
 * Edited on May 22, 2014, 10:18pm
 * Created on May 5, 2014, 8:57 AM
 */


/*******************************************************************************
 *                                 INCLUDES                                    *
 ******************************************************************************/
#include "cNeuroTouch.h"

#define ACTIVATE_SS_DEVICE


/*******************************************************************************
 *                            PRIVATE #DEFINES                                 *
 ******************************************************************************/	
//#define DEBUG_PRINTF // Debug print flag, when defined, print statements allowed

// DLL for the PCI Express card: S626 //
#ifndef DLLVERSIONINFO
typedef struct _DllVersionInfo
{
	DWORD cbSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformID;
}DLLVERSIONINFO;
#endif

#ifndef DLLGETVERSIONPROC
typedef int (FAR WINAPI *DLLGETVERSIONPROC) (DLLVERSIONINFO *);
#endif
//-------------------------- END OF PRIVATE DEFINES ----------------------------




/*******************************************************************************
 *							   PRIVATE VARIABLES                               *
 ******************************************************************************/

// PCI Board Definitions //
const unsigned int board = 0; // the board handle number (if single, 0)

// Encoder Variables //
static double zeroEncoderParamA = 0; // This should be subtracted from the raw encoder value to zero for Motor A
static double zeroEncoderParamB = 0; // "	"	"	"	"	"	

// Jacobian Defines for Force-Torque relationship //
static float Jacobian[2][2] =
    {
        {-63.4625,  -109.3659},
        {-173.4330,  257.9403}
    };
static float Jacobian_Transpose[2][2] = 
{
	{Jacobian[0][0], Jacobian[1][0]},
	{Jacobian[0][1], Jacobian[1][1]}

};

// Force-Torque defines and limits //
static float Fx_Max = 3; // [N], largest force that can be displayed at end effector (use simulation to obtain)
static float Fy_Max = 0;  // [N], largest force that can be displayed at end effecotr (use simulation to obtain)

//PWM Servo Motor Driver Defines //
static float Kt						 = 23.2;						// [mNm/A] Torque Constant 23.2 mNm/A
static float I_max_limit  			 = 1.1;							// [A] Current limit as set on Driver Amplifier Board
static float Torque_max_motor		 = I_max_limit*Kt;				// [Nm] Maximum continuous torque that can be output by motors based on current limit (1.1A * Kt)
static float GearRatio				 = 13;							// Capstan Drive Gear Ratio
static float Torque_max_capstan_link = Torque_max_motor*GearRatio;  // [Nm] Maximum torque that can be output on abstract joints of capstan
static float Kloopgain				 = .22;							// Loop gain of the Driver Board : I = Kloopgain * Vinput
static float Vout_max_5				 = 5;							// maximum allowed output voltage commanded from the S626 to the driver board
static float Vout_max_10			 = 10;							// maximum allowed output voltage from the Driver Board


// Poll List for ADC setup //
BYTE poll_list[2] = 
	{
		{1 | RANGE_5V},			// Channel 1, Limit input range of 5v( ON PIN 6 and 5 J1)--> Current Monitor Pin on Driver Board
		{2 | RANGE_10V | EOPL}  // channel 2, limit input range of 10V, mark as end of list( pin 8 and 7on J1)--> Current Ref Pin on Driver Board
	};

SHORT databuf[16]; // Buffer to recieve the data returned from an A/D read


// Module level variables for Query Functions
static float Torque_MA = 0; // Motor A Torque last commanded
static float Torque_MB = 0; // Motor B Torque last commanded
static float Torque_CA = 0; // Capstan A Torque last commanded
static float Torque_CB = 0; //Capstan B Torque last commanded

static float Position_MA = 0; // Theta associated with Motor A
static float Position_MB = 0; // Theta associated with Motor B

static float Force_EE_X = 0;  // Force commanded in x direction of end effector
static float Force_EE_Y = 0;  // Force commanded in y direction of end effector
static float Timestamp  = 0;  // Time_Stamp

//--------------------------------- END OF PRIVATE VARIABLES ----------------




/*******************************************************************************
 *							PRIVATE FUNCTIONS                                  *
 ******************************************************************************/

//===========================================================================
// Constructor
//===========================================================================
cNeuroTouch::cNeuroTouch(unsigned int a_deviceNumber)
{
	m_deviceID = a_deviceNumber;
}

//===========================================================================
// Destructor
//===========================================================================
cNeuroTouch::~cNeuroTouch()
{
	this->setForce(0,0);
}





/*******************************************************************************
 *                          PUBLIC FUNCTIONS                                   *
 ******************************************************************************/
/****************************************************************************
 Function
     Open()

 Parameters
    None

 Returns
     Integer (O exit fine, 1 error)

 Description
    Opens connection to the Sensoray S626 board.
	Configures the left and right motors' counters on the S626 to read
	quadrature encoder signals. 

 Notes
     To activate the NeuroTouch, must define ACTIVATE_SS_DEVICE in header.
	 Looking at Neurotouch from the front:
	 Left Motor Encoder  (A) --> CNTR 0A
	 Right Motor Encoder (B) --> CNTR 1A

	 Pin Configuration:
	 Left Motor : [A+ GND B+ VCC I+] = [2 3 5 6 8] pins on J5 (Appendix C)
	 Right Motor: [A+ GND B+ VCC I+] = [11 9 14 12 17] pins on J5 (Appendix C)

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::open()
{
#ifdef ACTIVATE_SS_DEVICE //defined in header file
	// load the .dll
	S626_DLLOpen();
	#ifdef DEBUG_PRINTF
		std::cout << "OPENED DLL\n\n";
	#endif

	// open the 626 card
	S626_OpenBoard(0,0,0,0);
	#ifdef DEBUG_PRINTF
		std::cout << "OPENED Board 626\n\n";
	#endif

	//check for any errors in opening board
	if (S626_GetErrors(0) != 0){
		std::cout << "ERROR IN OPENING BOARD\n\n";
		return S626_GetErrors(0);
	}
	
	// Encoder Configurations //

	/**************** MOTOR A ******************/
	// Set counter operating mode.
    S626_CounterModeSet( 0, CNTR_0A , // set counter for channel 0A
    ( INDXSRC_SOFT << BF_INDXSRC )	|
    ( INDXPOL_POS << BF_INDXPOL )	| // Active high index.
	( CLKSRC_COUNTER << BF_CLKSRC ) | // Operating mode is Counter.
	( CLKPOL_POS << BF_CLKPOL )		| // Active high clock.
	( CLKMULT_4X << BF_CLKMULT )	| // Clock multiplier is 4x.
    ( CLKENAB_ALWAYS << BF_CLKENAB ) ); // Counting is always enabled.

	// Initialize preload value to zero so that the counter core will be set
	// to zero upon the occurance of an Index.
    S626_CounterPreload( 0, CNTR_0A, 0 );

	// Enable latching of accumulated counts on demand. This assumes that
	// there is no conflict with the latch source used by paired counter 2B.
    S626_CounterLatchSourceSet( 0, CNTR_0A, LATCHSRC_AB_READ );

	// Enable the counter to generate interrupt requests upon index.
    S626_CounterIntSourceSet( 0, CNTR_0A, INTSRC_INDX );

	// Simulate Index Pulse to reset count at 0 on startup
    S626_CounterSoftIndex(0, CNTR_0A);


	/**************** MOTOR B *******************/
	// Set counter operating mode.
    S626_CounterModeSet( 0, CNTR_1A,	// set counter for channel 1A
    ( INDXSRC_SOFT << BF_INDXSRC )	|
    ( INDXPOL_POS << BF_INDXPOL )	|	// Active high index.
	( CLKSRC_COUNTER << BF_CLKSRC ) |	// Operating mode is Counter.
	( CLKPOL_POS << BF_CLKPOL )		|	// Active high clock.
	( CLKMULT_4X << BF_CLKMULT )	|	// Clock multiplier is 4x.
    ( CLKENAB_ALWAYS << BF_CLKENAB ) ); // Counting is always enabled.

	// Initialize preload value to zero so that the counter core will be set
	// to zero upon the occurance of an Index.
    S626_CounterPreload( 0, CNTR_1A, 0 );

	// Enable latching of accumulated counts on demand. This assumes that
	// there is no conflict with the latch source used by paired counter 2B.
    S626_CounterLatchSourceSet( 0, CNTR_1A, LATCHSRC_AB_READ );

	// Enable the counter to generate interrupt requests upon index.
    S626_CounterIntSourceSet( 0, CNTR_1A, INTSRC_INDX );

	// Simulate Index Pulse to reset count at 0 on startup
    S626_CounterSoftIndex(0, CNTR_1A);


#endif // End on ACTIVATE_SS_DEVICE

    return 0;
}




/****************************************************************************
 Function
     close()

 Parameters
    None

 Returns
     Integer (O exit fine, 1 error)

 Description
    Closes connection to the Sensoray S626 board.
	Unregisters Board number 0 after programming analog and digital outputs
	to their application-defined shut-down states

 Notes
     Sets the motor forces to zero, should also disable any force sensors.
	 In order to close the device, ACTIVATE_SS_DEVICE must be defined

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::close()
{
#ifdef ACTIVATE_SS_DEVICE
	// Program all analog outputs to zero volts.
	for ( WORD DacChan = 0; DacChan < 4; S626_WriteDAC( 0, DacChan++, 0 ));
	
	// Program all digital outputs to the inactive state.
	for ( WORD Group = 0; Group < 3; S626_DIOWriteBankSet( 0, Group++, 0 ));
	
	// Unregister the board and release its handle.
	S626_CloseBoard( 0 );

	// close the DLL
	S626_DLLClose();
#endif

	return 0;
}



/****************************************************************************
 Function
     getPosition()

 Parameters
    base address of the A and B position variables (position is theta)

 Returns
     Integer (O exit fine, 1 error)

 Description
    Reads values from the encoders and deciphers it using logic to obtain
	the angles relative to the zero'd position.

 Notes
     All position is returned in degrees. In order to return in radians,
	 just uncomment the radians conversion below.

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::getPosition(float& a_position, float& b_position)
{
#ifdef ACTIVATE_SS_DEVICE
	// Read in value from Encoder for Motor A and B
    unsigned long EncoderRawA = S626_CounterReadLatch(0, CNTR_0A); // Motor A
	unsigned long EncoderRawB = S626_CounterReadLatch(0, CNTR_1A); // Motor B

	// Change the raw encoder count into a double and center the total count 
	// around 0
	double EncoderPosA = 0;
	if (EncoderRawA < ((MAX_COUNT+1)/2)){ // If the encoder count hasn't gone in the negative direction
		EncoderPosA = EncoderRawA; //set the raw encoder value to PosA
		
	}
	else if (EncoderRawA >= ((MAX_COUNT+1)/2)){ // If the encoder count has gone negative
		EncoderPosA = -MAX_COUNT+1+(double)EncoderRawA; // account for negative angle by subtracting from Maxcount
	}
	
	// Repeat for Motor B
	double EncoderPosB = 0;
	if (EncoderRawB < ((MAX_COUNT+1)/2))
		EncoderPosB = EncoderRawB;
	else if (EncoderRawB >= ((MAX_COUNT+1)/2))
		EncoderPosB = -MAX_COUNT+1+(double)EncoderRawB;
	
	/*
    // Return encoder position as a double in radian
    a_position = EncoderPosA * ENCCOUNT_TO_RAD;
	b_position = EncoderPosB * ENCCOUNT_TO_RAD;
	*/

	// Or Return encoder position as a double in degrees
	a_position = EncoderPosA * ENCCOUNT_TO_DEG - zeroEncoderParamA;
	b_position = EncoderPosB * ENCCOUNT_TO_DEG - zeroEncoderParamB;

	// Debug 
	#ifdef DEBUG_PRINTF
		printf("RAW A: %d   RAW B: %d\n",EncoderRawA, EncoderRawB);	
		printf("Encoder PosA : %f      PosB: %f\n\n",a_position, b_position);
	#endif
	


#else // If the NeuroTouch device is not defined
    a_position = 0;
	b_position = 0;
#endif


	return 0;
}



/****************************************************************************
 Function
    setForce()

 Parameters
    base address of the desired x and y force

 Returns
     Integer (O exit fine, 1 error)

 Description
    Uses the Jacobian Transpose to calculate the desired torques to be displayed 
	at each motor.

 Notes
     
 Author
 Darrel R. Deo
 ****************************************************************************/
//===========================================================================
// Set the force/torque of the skin stretch actuator
//===========================================================================
int cNeuroTouch::setForce(const double& x_force, const double& y_force)
{

	// Darrel Addition
#ifdef ACTIVATE_SS_DEVICE
	// define the voltage to be output on motorA and motorB
	float VoltOutA = 0;
	float VoltOutB = 0;

	//using the desired forces and the jacobian transpose, compute the desired torques
	// NOTE: Matlab definitions of Motor 1 and Motor 2 are equivalent to this scripts MotorA and MotorB respectively
	float Torque_Capstan_A = 0; // define the torque variables, A-->motor 1 in matlab simulation
	float Torque_Capstan_B = 0; // B --> Motor 2 in matlab simulation 

	//Desired Capstan joint Torques
	//NOTE : These torques are in mNm
	Torque_Capstan_A = Jacobian_Transpose[0][0]*x_force + Jacobian_Transpose[0][1]*y_force; // For motor A, set equal to T1
	Torque_Capstan_B = Jacobian_Transpose[1][0]*x_force + Jacobian_Transpose[1][1]*y_force; // For motor B, set equal to T2

	// Using Gear ratio, find the desired motor torques
	float Torque_Motor_A = (Torque_Capstan_A/GearRatio);
	float Torque_Motor_B = (Torque_Capstan_B/GearRatio);

	// based on the ratio of the desired torque and the maximum torque that can be output, scale the command voltage to the 
	// amplifier appropriately
	VoltOutA = Vout_max_5*(Torque_Motor_A / Torque_max_motor);
	VoltOutB = Vout_max_5*(Torque_Motor_B / Torque_max_motor);

	// Check to make sure the voltage does not exceed the range of 5 volts
	if(VoltOutA > 5) VoltOutA = 5;
	if(VoltOutB > 5) VoltOutB = 5;

	if(VoltOutA < -5) VoltOutA = -5;
	if(VoltOutB < -5) VoltOutB = -5;

	// Write the voltages out to the DAC
	S626_WriteDAC(board, DAC1, VoltOutA*DAC_VSCALAR); // Write the MotorA voltage out to DAC1 Motor A
	S626_WriteDAC(board, DAC2, VoltOutB*DAC_VSCALAR); // Write the MotorB voltage out to DAC2 Motor B

#endif

	//Debug Prints
	#ifdef DEBUG_PRINTF
		printf("DESIRED X FORCE: %f			DESIRED Y FORCE: %f\n",(float)x_force, (float)y_force);
		printf("CAP TORQUE A :   %f			CAP TORQUE B :   %f\n",Torque_Capstan_A, Torque_Capstan_B);
		printf("MOTORA TORQUE:   %f			MOTORB TORQUE:   %f\n",Torque_Motor_A, Torque_Motor_B);
		printf("VOLT OUT A:      %f			VOLT OUT B:      %f\n", VoltOutA, VoltOutB);
	#endif

	// Update module level variables for use with query functions
	 Torque_MA = Torque_Motor_A; // Motor A Torque last commanded
	 Torque_MB = Torque_Motor_B; // Motor B Torque last commanded
	 Torque_CA = Torque_Capstan_A; // Capstan A Torque last commanded
	 Torque_CB = Torque_Capstan_B; //Capstan B Torque last commanded

	 getPosition(Position_MA, Position_MB);

	 Force_EE_X = x_force;  // Force commanded in x direction of end effector
	 Force_EE_Y = y_force;  // Force commanded in y direction of end effector
	 Timestamp  = 0;  // Time_Stamp, Don't really know how to implement


    return 0;
}




/****************************************************************************
 Function
    zeroEncoders()

 Parameters
    None

 Returns
     Integer (O exit fine, 1 error)

 Description
    Captures the current angle in the left and right Motor Encoders. Stores
	into a module level variable that is used to zero the angle at that point. 

 Notes
     The desired position to be the zero point must be held in place as this function 
	 is called.

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::zeroEncoders(void){

	// obtain the current value of the encoders at this point and store them in the variable zeroEncoderParam
    unsigned long EncoderRaw = S626_CounterReadLatch(0, CNTR_0A);
	
	// store into module level variable 
	if (EncoderRaw < ((MAX_COUNT+1)/2)){ // if the current angle is positive
		zeroEncoderParamA = EncoderRaw;
		
	}
	else if (EncoderRaw >= ((MAX_COUNT+1)/2)) // if the current angle is negative
		zeroEncoderParamA = -MAX_COUNT+1+(double)EncoderRaw;


	// Repeat for Motor B
	unsigned long EncoderRawB = S626_CounterReadLatch(0, CNTR_1A);

	// store into module level variable 
	if (EncoderRawB < ((MAX_COUNT+1)/2)){
		zeroEncoderParamB = EncoderRawB;
		
	}
	else if (EncoderRawB >= ((MAX_COUNT+1)/2))
		zeroEncoderParamB = -MAX_COUNT+1+(double)EncoderRawB;


	// Convert to degress using conversion factor
	zeroEncoderParamB = zeroEncoderParamB * ENCCOUNT_TO_DEG;
	zeroEncoderParamA = zeroEncoderParamA * ENCCOUNT_TO_DEG;

	//Debug
	#ifdef DEBUG_PRINTF
		printf("Raw A %d, Raw B %d  \n\n",EncoderRaw, EncoderRawB);
	#endif

	return 0;

}





/****************************************************************************
 Function
   initADC()

 Parameters
    None

 Returns
     Integer (O exit fine, 1 error)

 Description
    Calls the ResetADC function with the current poll list (defined on top) to config
	AD channels

 Notes
     Poll list must be declared on top of file in order for this to work

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::initADC(void){
	S626_ResetADC(0, poll_list); // pass poll list to driver to initialize appropriately
	return 0;
}





/****************************************************************************
 Function
   retrieveState()

 Parameters
    Torque and position variables of interest

 Returns
     Integer (O exit fine, 1 error)

 Description
    Updates the passed in variables with the last commanded torques, forces, and measured positions

 Notes
     Time stamp has not been implemented, but can be coordinated on the master side

 Author
 Darrel R. Deo
 ****************************************************************************/
int cNeuroTouch::retrieveState(float& TorqueMA, float& TorqueMB, float& TorqueCA, float& TorqueCB, float& PositionMA, float& PositionMB, float& ForceEEx, float& ForceEEy, float& TimeStamp){
	// obtain the current State
	TorqueMA = Torque_MA;
	TorqueMB = Torque_MB;
	TorqueCA = Torque_CA;
	TorqueCB = Torque_CB;
	PositionMA= Position_MA;
	PositionMB= Position_MB;
	ForceEEx = Force_EE_X;
	ForceEEy = Force_EE_Y;
	TimeStamp = 0;

	return 0;

}



/*************** TEST HARNESSES ********************************/
int cNeuroTouch::testHarnessADC(void){
	// Set a command voltage out
	S626_WriteDAC(board, DAC1, (float)5 * DAC_VSCALAR); // Write 5V out to DAC1
	for( ;; ){
		// obtain the contents of the AD reg
		S626_ReadADC( 0, databuf);
		float val = ((float)databuf[0]*(float)Vout_max_5)/(float)MAX_AD_VALUE;
		float val2 = ((float)databuf[1]*(float)Vout_max_10)/(float)MAX_AD_VALUE;

		printf("THE CURRENT OUTPUT IS : %f\r\n", (float)val*(2.2));//(3.8/2));//2.2 );
		//printf("THE CURR REF Voltage input IS : %f\r\n", (float)val2);
		Sleep(500);
	}
}
