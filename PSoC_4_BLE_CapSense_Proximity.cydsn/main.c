/******************************************************************************
* Project Name		: PSoC_4_BLE_CapSense_Proximity
* File Name			: main.c
* Version 			: 1.0
* Device Used		: CY8C4247LQI-BL483
* Software Used		: PSoC Creator 3.1
* Compiler    		: ARM GCC 4.8.4, ARM RVDS Generic, ARM MDK Generic
* Related Hardware	: CY8CKIT-042-BLE Bluetooth Low Energy Pioneer Kit 
* Owner				: ROIT
*
********************************************************************************
* Copyright (2014), Cypress Semiconductor Corporation. All Rights Reserved.
********************************************************************************
* This software is owned by Cypress Semiconductor Corporation (Cypress)
* and is protected by and subject to worldwide patent protection (United
* States and foreign), United States copyright laws and international treaty
* provisions. Cypress hereby grants to licensee a personal, non-exclusive,
* non-transferable license to copy, use, modify, create derivative works of,
* and compile the Cypress Source Code and derivative works for the sole
* purpose of creating custom software in support of licensee product to be
* used only in conjunction with a Cypress integrated circuit as specified in
* the applicable agreement. Any reproduction, modification, translation,
* compilation, or representation of this software except as specified above 
* is prohibited without the express written permission of Cypress.
*
* Disclaimer: CYPRESS MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH 
* REGARD TO THIS MATERIAL, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* Cypress reserves the right to make changes without further notice to the 
* materials described herein. Cypress does not assume any liability arising out 
* of the application or use of any product or circuit described herein. Cypress 
* does not authorize its products for use as critical components in life-support 
* systems where a malfunction or failure may reasonably be expected to result in 
* significant injury to the user. The inclusion of Cypress' product in a life-
* support systems application implies that the manufacturer assumes all risk of 
* such use and in doing so indemnifies Cypress against all charges. 
*
* Use of this Software may be limited by and subject to the applicable Cypress
* software license agreement. 
*******************************************************************************/

/******************************************************************************
*                           THEORY OF OPERATION
*******************************************************************************
* The project will show case the capability of PSoC 4 BLE to communicate with
* a BLE Central device. The data communicated is the CapSense proximity value,
* sent from PSoC 4 BLE over a custom service. This custom service allows
* notifications to be sent to central device after notifications are enabled.
* This project utilizes CapSense component to detect the change in capacitance
* of the proximity sensor and report this to central device over BLE.
* The BLE central device can be either BLE capable smartphone with associated 
* mobile application or CySmart PC tool.
* This project also inludes low power mode operation, idle for battery operated 
* devices. The project utlizes Deep Sleep feature of both BLESS and CPU to remain 
* in low power mode as much as possible, while maintaining the BLE connection and  
* data transfer. This allows the device to run on coin cell battery for long time.
*
* Note:
* The programming pins have been configured as GPIO, and not SWD. This is because 
* when programming pins are configured for SWD, then the silicon consumes extra
* power through the pins. To prevent the leakage of power, the pins have been set 
* to GPIO. With this setting, the kit can still be acquired by PSoC Creator or
* PSoC Programmer software tools for programming, but the project cannot be 
* externally debugged. To re-enable debugging, go to PSoC_4_BLE_CapSense.cydwr from
* Workspace Explorer, go to Systems tab, and set the Debug Select option to 'SWD'.
* Build and program this project to enable external Debugging.
*
* Refer to BLE Pioneer Kit user guide for details.
*******************************************************************************
* Hardware connection required for testing -
* PROX pin 		- P2[0] (requires an external wire-loop connection to PROX sensor)
* Cmod pin		- P4[0] (hard-wired in the PSoC 4 BLE Module)
* User Button	- P2[7] (hard-wired on BLE Pioneer Kit)
* Status LED	- P3[7] (hard-wired on BLE Pioneer Kit)
******************************************************************************/
#include <main.h>
#include "WatchdogTimer.h"
#include <BLEApplications.h>
/*'deviceConnected' flag is used by application to know whether a Central device  
* has been connected. This is updated in BLE event callback */
extern uint8 deviceConnected;

/*'startNotification' flag is set when the central device writes to CCC (Client  
* Characteristic Configuration) of the CapSense proximity characteristic to 
* enable notifications */
extern uint8 startNotification;	

/* 'restartAdvertisement' flag is used to restart advertisement */
extern uint8 restartAdvertisement;

/* 'initializeCapSenseBaseline' flag is used to call the function once that initializes 
* all CapSense baseline. The baseline is initialized when the first advertisement 
* is started. This is done so that any external disturbance while powering the kit does 
* not infliuence the baseline value, that may cause wrong readings. */
uint8 initializeCapSenseBaseline = TRUE;

    static uint32 currentTimestamp = 0;
    CYBLE_LP_MODE_T bleMode;
    uint8 interruptStatus;
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*        System entrance point. This calls the BLE routine functions as well as
* function for handling CapSense Proximity changes
*
* Parameters:
*  void
*
* Return:
*  int
*

*******************************************************************************/
int main()
{
    /* This function will initialize the system resources such as BLE and CapSense */
	InitializeSystem();
	 currentTimestamp = WatchdogTimer_GetTimestamp(); 
    for(;;)
    {
		/*Process Event callback to handle BLE events. The events generated and 
		* used for this application are inside the 'CustomEventHandler' routine*/
		CyBle_ProcessEvents();
		
		/* Function to handle LED status depending on BLE state */
		HandleStatusLED();
		
		/* Handle proximity data and CCCD value update only if BLE device is connected */
		if(TRUE == deviceConnected) 
		{	
			/* When the Client Characteristic Configuration descriptor (CCCD) is written
			* by Central device for enabling/disabling notifications, then the same
			* descriptor value has to be explicitly updated in application so that
			* it reflects the correct value when the descriptor is read */
			UpdateNotificationCCCD();
			
			/* If CapSense Initialize Baseline API has not been called yet, call the
			* API and reset the flag. */
			if(initializeCapSenseBaseline)
			{
				/* Reset 'initializeCapSenseBaseline' flag*/
				initializeCapSenseBaseline = FALSE;
				
				/* Initialize all CapSense Baseline */
				CapSense_InitializeAllBaselines();
			}
			
			/* Send proximity data when notifications are enabled */
			if(startNotification & CCCD_NTF_BIT_MASK)
			{
				/*Check for CapSense proximity change and report to BLE central device*/
				HandleCapSenseProximity();
			}
		}

		if(restartAdvertisement)
		{
			/* Reset 'restartAdvertisement' flag*/
			restartAdvertisement = FALSE;
			
			/* Start Advertisement and enter Discoverable mode*/
			CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);	
		}
         /* Try to stay in low power mode until the next watchdog interrupt */
        while(WatchdogTimer_GetTimestamp() == currentTimestamp)
        {
            /* Process any pending BLE events */
            CyBle_ProcessEvents();
            
            /* The idea of low power operation is to first request the BLE 
             * block go to Deep Sleep, and then check whether it actually
             * entered Deep Sleep. This is important because the BLE block
             * runs asynchronous to the rest of the application and thus
             * could be busy/idle independent of the application state. 
             * 
             * Once the BLE block is in Deep Sleep, only then the system 
             * can enter Deep Sleep. This is important to maintain the BLE 
             * connection alive while being in Deep Sleep.
             */

            
            /* Request the BLE block to enter Deep Sleep */
            bleMode = CyBle_EnterLPM(CYBLE_BLESS_DEEPSLEEP);

            
            /* Check if the BLE block entered Deep Sleep and if so, then the 
             * system can enter Deep Sleep. This is done inside a Critical 
             * Section (where global interrupts are disabled) to avoid a 
             * race condition between application main (that wants to go to 
             * Deep Sleep) and other interrupts (which keep the device from 
             * going to Deep Sleep). 
             */
            interruptStatus = CyEnterCriticalSection();
            
            /* Check if the BLE block entered Deep Sleep */
            if(CYBLE_BLESS_DEEPSLEEP == bleMode)
            {
                /* Check the current state of BLE - System can enter Deep Sleep
                 * only when the BLE block is starting the ECO (during 
                 * pre-processing for a new connection event) or when it is 
                 * idle.
                 */
                if((CyBle_GetBleSsState() == CYBLE_BLESS_STATE_ECO_ON) ||
                   (CyBle_GetBleSsState() == CYBLE_BLESS_STATE_DEEPSLEEP))
                {
                    CySysPmDeepSleep();
                }
            }
            /* The else condition signifies that the BLE block cannot enter 
             * Deep Sleep and is in Active mode.  
             */
            else
            {
                /* At this point, the CPU can enter Sleep, but Deep Sleep is not
                 * allowed. 
                 * There is one exception - at a connection event, when the BLE 
                 * Rx/Tx has just finished, and the post processing for the 
                 * connection event is ongoing, the CPU cannot go to sleep.
                 * The CPU should wait in Active mode until the post processing 
                 * is complete while continuously polling the BLE low power 
                 * entry. As soon as post processing is complete, the BLE block 
                 * would enter Deep Sleep (because of the polling) and the 
                 * system Deep Sleep would then be entered. Deep Sleep is the 
                 * preferred low power mode since it takes much lesser current.
                 */
                if(CyBle_GetBleSsState() != CYBLE_BLESS_STATE_EVENT_CLOSE)
                {
                    CySysPmSleep();
                }
            }
            
            /* Exit Critical section - Global interrupts are enabled again */
            CyExitCriticalSection(interruptStatus);
        }

        /* Hibernate entry point - Hibernate is entered upon a BLE disconnect
         * event or advertisement timeout. Wakeup happens via SW2 switch press, 
         * upon which the execution starts from the first line of code. 
         * The I/O state, RAM and UDBs are retained during Hibernate.
         */
        if(enterHibernateFlag)
        {
            /* Stop the BLE component */
            CyBle_Stop();
            
            /* Enable the Hibernate wakeup functionality */
            SW2_Switch_ClearInterrupt();
            Wakeup_ISR_Start();
            
            
            
            /* Enter hibernate mode */
            CySysPmHibernate();
        }
    }	/* This is end of for(;;) */
}

/*******************************************************************************
* Function Name: InitializeSystem
********************************************************************************
* Summary:
*        Starts the components and initializes CapSense baselines
*
* Parameters:
*  void
*
* Return:
*  void
*

*******************************************************************************/
void InitializeSystem(void)
{
	/* Enable global interrupt mask */
	CyGlobalIntEnable;

	/* Start BLE component and register the CustomEventHandler function. This 
	* function exposes the events from BLE component for application use */
	CyBle_Start(CustomEventHandler);					
	
	/* Set drive mode of the status LED pin to Strong*/
	Status_LED_SetDriveMode(Status_LED_DM_STRONG);	
	
	#ifdef CAPSENSE_ENABLED
	/* Enable the proximity widget explicitly and start CapSense component. 
	* Initialization of the baseline is done when the first connection 
	* happens  */
	CapSense_EnableWidget(CapSense_PROXIMITYSENSOR0__PROX);
	CapSense_Start();
    WatchdogTimer_Start();
	#endif
		
}

/*******************************************************************************
* Function Name: HandleCapSenseProximity
********************************************************************************
* Summary:
*       Read the proximity data from the sensor and update the BLE
*		custom notification value.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void HandleCapSenseProximity(void)
{
	#ifdef CAPSENSE_ENABLED
	/* Static variable used as counter for reading the new proximity value */
	static uint16 proxCounter = TRUE;			
				
	/* 'proximityValue' stores the proximity value read from CapSense component */
	uint8 proximityValue;	
		
	/* Check if proxCounter has reached its counting value */
	if(FALSE == (--proxCounter))			
	{
		/* Set 'proxCounter' to the PROX_COUNTER_VALUE. This counter prevents sending 
		   of large number of CapSense proximity data to BLE Central device */
		proxCounter = PROX_COUNTER_VALUE;

		/* Scan the Proximity Widget */
		CapSense_ScanWidget(CapSense_PROXIMITYSENSOR0__PROX);				
		
		/* Wait for CapSense scanning to be complete. This could take about 5 ms */
		while(CapSense_IsBusy())
		{
		}
		
		/* Update CapSense Baseline */
		CapSense_UpdateEnabledBaselines();			

		/* Get the Diffcount between proximity raw data and baseline */
		proximityValue = CapSense_GetDiffCountData(CapSense_PROXIMITYSENSOR0__PROX);
		
		/* Send the proximity data to BLE central device by notifications*/
		SendDataOverCapSenseNotification(proximityValue);
			
	}
	#endif
}

/* [] END OF FILE */
