/** @file sim808.c
*  @brief Implementations of functions that communicate directly with the SIM808 module and control it.
*
*  @author xuanviet
*  @bug issue #9
*/
#include <string.h>
#include <stdio.h>
#include "sim808.h"


UART_HandleTypeDef huart1; 
UART_HandleTypeDef huart2;



/* These Global variables should only be touched by sim_send_cmd, get_cmd_reply, UART_receive_IT,*/
static volatile uint8_t rx_byte; /* The receive interupt routine uses rx_byte to store a copy of the received byte*/
static volatile uint8_t rx_index=0; /*track the number of received bytes.*/
static volatile char sim_rx_buffer[RX_BUFFER_LENGTH];

void  send_debug(const char * debug_msg)
{
	char debug_prompt[]="Debug > ";
	HAL_UART_Transmit(&debug_uart,(uint8_t*)debug_prompt,strlen(debug_prompt),TX_TIMEOUT);
	HAL_UART_Transmit(&debug_uart,(uint8_t*)debug_msg,strlen(debug_msg),TX_TIMEOUT);
	HAL_UART_Transmit(&debug_uart,(uint8_t*)"\r\n",2,TX_TIMEOUT);
}

void  send_raw_debug(uint8_t * debug_dump,uint8_t length)
{
	char debug_prompt[]="Debug > ";
	HAL_UART_Transmit(&debug_uart,(uint8_t*)debug_prompt,strlen(debug_prompt),TX_TIMEOUT);
	HAL_UART_Transmit(&debug_uart,debug_dump,length,TX_TIMEOUT);
	HAL_UART_Transmit(&debug_uart,(uint8_t*)"\r\n",2,TX_TIMEOUT);
}



/**
 * @brief is called when the receive buffer of any UART has received 1 byte.
 * it copies the received byte into the receive buffer which is a global variable: sim_rx_buffer. 
 * it also increases by one the counter of received bytes: rx_index. Which points to the next free index in the receive buffer.
 * usart1 is always used to communicate with SIM module therefore AT_uart is defined as usart1.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	if (huart->Instance==USART1){
		sim_rx_buffer[rx_index++ ]=rx_byte; /*rx_index++ modulo RX_BUFFER_LENGTH to ensure it always stays smaller then RX_BUFFER_LENGTH*/
		
		/* Enable UART receive interrupt again*/
		HAL_UART_Receive_IT(&AT_uart,(uint8_t *)&rx_byte,1);
	}
}	


		 
uint8_t sim_init(SIM808_typedef * sim){

	/*Initialize UART peripherals*/

	debug_uart.Instance = USART2;
	debug_uart.Init.BaudRate = BAUD_RATE;
	debug_uart.Init.WordLength = UART_WORDLENGTH_8B;
	debug_uart.Init.StopBits = UART_STOPBITS_1;
	debug_uart.Init.Parity = UART_PARITY_NONE;
	debug_uart.Init.Mode = UART_MODE_TX_RX;
	debug_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	debug_uart.Init.OverSampling = UART_OVERSAMPLING_16;
	debug_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	debug_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&debug_uart) != HAL_OK){
		Error_Handler();
	}	


	AT_uart.Instance = USART1;
	AT_uart.Init.BaudRate = BAUD_RATE;
	AT_uart.Init.WordLength = UART_WORDLENGTH_8B;
	AT_uart.Init.StopBits = UART_STOPBITS_1;
	AT_uart.Init.Parity = UART_PARITY_NONE;
	AT_uart.Init.Mode = UART_MODE_TX_RX;
	AT_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	AT_uart.Init.OverSampling = UART_OVERSAMPLING_16;
	AT_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	AT_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&AT_uart) != HAL_OK){
	Error_Handler();
	}

	/* trigger an interrupt for every received byte on AT_uart*/
	HAL_UART_Receive_IT(&AT_uart,(uint8_t *)&rx_byte,1);  

	/*Initialize GPIOs*/
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/*Configure Reset GPIO pin */
	HAL_GPIO_WritePin(sim->reset_gpio, sim->reset_pin, GPIO_PIN_SET);
	GPIO_InitStruct.Pin = sim->reset_pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(sim->reset_gpio, &GPIO_InitStruct);

	/*Configure  power on GPIO pin */
	HAL_GPIO_WritePin(sim->power_on_gpio, sim->power_on_pin, GPIO_PIN_SET);
	GPIO_InitStruct.Pin = sim->power_on_pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(sim->power_on_gpio, &GPIO_InitStruct);

	/*Configure  STATUS GPIO pin */
	GPIO_InitStruct.Pin = sim->status_pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(sim->status_gpio, &GPIO_InitStruct);

	/* Power on module:
	 * Check if the module is Powered on by reading the value of STATUS pin.
	 * If the module is powered off then pull down the power pin of sim808 for at least 1 second to power it on.
	 * Then read the STATUS pin again. If the device is not powered on, try 2 more times.
	 */

	/* Count number of power on trials */
	uint8_t trials=0;  

	while(!HAL_GPIO_ReadPin(sim->status_gpio,sim->status_pin) && trials<3){
		HAL_GPIO_WritePin(sim->power_on_gpio,sim->power_on_pin,GPIO_PIN_RESET);
		/* Keep pin down for 1.2 s */
		HAL_Delay(1200);
		HAL_GPIO_WritePin(sim->power_on_gpio,sim->power_on_pin,GPIO_PIN_SET);
		HAL_Delay(100);
		trials++;
	}
	
	#ifdef DEBUG_MODE
		send_debug("System initialization: Started");
	#endif
	/* Read STATUS pin of the SIM808 to check the power on status. if the module is ON then power on indicator LED*/
	if (HAL_GPIO_ReadPin(sim->status_gpio,sim->status_pin)){
		HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_SET);

	/* Send the string "AT" to synchronize baude rate of the SIM808 module*/
	HAL_UART_Transmit(&AT_uart,(uint8_t *)"AT\r",3,TX_TIMEOUT);

	/*It is recommended to wait 3 to 5 seconds before sending the first AT character. */
	HAL_Delay(3000);	
	} 
	else{
				#ifdef DEBUG_MODE
					send_debug("System initialization: FAILED");
					send_debug("Reason: SIM808 cannot be powered on");
				#endif
				/*Turn down the indicator LED and return FAIL */
				HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_RESET);
				return FAIL;
	}

	/*Send the First AT Command to check if the module is responding*/
	uint8_t is_module_replying=0;
	is_module_replying=send_AT_cmd("AT\r","OK",0,NULL,RX_TIMEOUT);
	if (is_module_replying==1){
			#ifdef DEBUG_MODE
				send_debug("System initialization: SUCCESS");
				send_debug("SIM808 module is responsive");
			#endif
		return SUCCESS;
	}
	else
	{
		#ifdef DEBUG_MODE
			send_debug("System initialization: FAILED");
			send_debug("Reason: SIM808 is not responding");
		#endif
		return FAIL;
	}
}

uint8_t sim_power_off(SIM808_typedef * sim){
			#ifdef DEBUG_MODE
				send_debug("Power off Module initiated");
			#endif
	
		uint8_t trials=0;  

		while(HAL_GPIO_ReadPin(sim->status_gpio,sim->status_pin) && trials<3){
		HAL_GPIO_WritePin(sim->power_on_gpio,sim->power_on_pin,GPIO_PIN_RESET);
		/* Keep pin down for 1.2 s */
		HAL_Delay(1200);
		HAL_GPIO_WritePin(sim->power_on_gpio,sim->power_on_pin,GPIO_PIN_SET);
		HAL_Delay(100);
		trials++;
	}
		
		if (HAL_GPIO_ReadPin(sim->status_gpio,sim->status_pin)==0){
			#ifdef DEBUG_MODE
			send_debug("Power off Module: SUCCESS");
			#endif
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_RESET);
			return SUCCESS;
		}
		else
		{
		#ifdef DEBUG_MODE
			send_debug("Power off Module: FAIL");
			#endif
			return FAIL;
		}
		
}

void system_reset(SIM808_typedef * sim){
	
	sim_power_off(sim);
	HAL_NVIC_SystemReset();
}


uint8_t send_AT_cmd(const char * cmd, const char * expected_reply, uint8_t save_reply, char * cmd_reply, uint32_t rx_timeout){
	
	uint8_t is_expected_reply_received=0;
	uint32_t timer=0;
	char  debug_msg[128];
	/* send the AT command via AT_uart */
	HAL_UART_Transmit(&AT_uart,(uint8_t *)cmd,strlen(cmd),TX_TIMEOUT);

	/* Wait for the module until the expected reply is received or if the timeout is breached */
	while ( (is_expected_reply_received==0) && (timer < rx_timeout)) {
		//is_expected_reply_received=strstr((const char *)sim_rx_buffer,expected_reply)==NULL?0:1;
		is_expected_reply_received=is_subarray_present((const uint8_t *)sim_rx_buffer,RX_BUFFER_LENGTH,(uint8_t *)expected_reply,strlen(expected_reply));
		timer++;
		HAL_Delay(1);
	}
	
	#ifdef DEBUG_MODE
	sprintf(debug_msg,"Finished in %d ms: ",timer);
	strcat(debug_msg,cmd);
	send_debug(debug_msg);
	#endif
	/* Note: 
	 * The reply from the module will be captured by the UART receive interrupt callback routine HAL_UART_RxCpltCallback() 
	 * and stored in the global array sim_rx_buffer[RX_BUFFER_LENGTH];
	 */	

	/* if save_reply is set to 1 then copy the reply from the global buffer to the parameter cmd_reply*/
	if (save_reply == 1 )
		memcpy(cmd_reply,(const char *)sim_rx_buffer,RX_BUFFER_LENGTH);

	/* clear the sim_rx_buffer, reset the receive counter rx_index 
	 * and then return 1 to acknowledge the success of the command		 
	 */
	memset((void *)sim_rx_buffer,NULL,RX_BUFFER_LENGTH);
	rx_index=0;
	
	return is_expected_reply_received;
	
}



uint8_t is_subarray_present(const uint8_t *array, size_t array_len, const uint8_t *subarray, size_t subarray_len)
{
    if (array_len < subarray_len) {
        return FALSE;
    }

    for (size_t i = 0; i <= array_len - subarray_len; ++i) {
        int match = TRUE;
        for (size_t j = 0; j < subarray_len; ++j) {
            if (array[i + j] != subarray[j]) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            return TRUE;
        }
    }

    return FALSE;
}



uint8_t send_serial_data(uint8_t * data, uint8_t length,  char * cmd_reply, uint32_t rx_timeout){
	
	
	
	HAL_UART_Transmit(&AT_uart,data,length,TX_TIMEOUT);
	
	
	uint8_t is_expected_reply_received=0;
	uint32_t timer=0;
	uint8_t expected_reply[]={0x53,0x45,0x4E,0x44,0x20,0x4F,0x4B}; /*SEND OK in HEX"*/

	/* Wait for the module until the expected reply is received or if the timeout is breached */
	/*strstr will not work here, because the buffer might contain raw hex data */
	while ( (is_expected_reply_received==0) && (timer < rx_timeout)) {
		is_expected_reply_received=is_subarray_present((uint8_t*)sim_rx_buffer,RX_BUFFER_LENGTH,expected_reply,7);
		timer++;
		HAL_Delay(1);
	}
	char  debug_msg[128];
	#ifdef DEBUG_MODE
		sprintf(debug_msg,"Finished in %d ms: ",timer);
		send_debug(debug_msg);
		send_debug(sim_rx_buffer);
	#endif
	
	/* copy buffer into local parameter and clear the sim_rx_buffer, reset the receive counter rx_index 
	 * and then return 1 to acknowledge the success of the command
	 */
	memcpy(cmd_reply,(const char *)sim_rx_buffer,RX_BUFFER_LENGTH);
	memset((void *)sim_rx_buffer,NULL,RX_BUFFER_LENGTH);
	rx_index=0;
	
	return 	is_expected_reply_received;
}


