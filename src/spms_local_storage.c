// Include common libraries
#include "spms_libs.h"
#include <drivers/uart.h>

// Globals
static airflow_local* localStorage;
static unsigned storageIndex = 0;

// Initialize the local storage
int local_storage_init() {
    localStorage = k_malloc(HEAP_SIZE * sizeof(airflow_local));
    if(!localStorage) {
        return -1;
    }
    else return 0;
}

// Add a new data set of airflow_local to the local storage
int add_sensor_series(airflow_local sensor_data) {
    if(storageIndex < 512) {
        memcpy(&localStorage[storageIndex], &sensor_data, sizeof(airflow_local));
        storageIndex++;
        return 0;
    }
    else {
        printk("\n\nERROR: Storage full!!\n");
        return -1;
    }
}

// Removes the local data at the given index. 
int remove_sensor_series(int index){
    if(index > 512) 
        {return -1;}
    for(int i = index;i<storageIndex;i++)
        { localStorage[i] = localStorage[i+1]; }
    storageIndex--;
    return 0;
}
// Get the local storage index of the next unassigned local storage entry
int get_local_storage_index() {
    return storageIndex;
}

// Get a local storage entry at a specific index
int get_sensor_series_index(int index, airflow_local* sensor_data) {
    if(storageIndex < 512) {
        memcpy(sensor_data, &localStorage[index], sizeof(airflow_local));
        return 0;
    }
    else {
        return -1;
    }
}

// int remove_local_storage_index(int index) {
//     if(index > 512)
//     {
//         return -1;
//     }
//     else if(index <= 512){
//         /* Copy next element value to current element */
//         for(int i=index-1; i<storageIndex-1; i++)
//         {
//             localStorage[i] = localStorage[i + 1];
//         }
//         /* Decrement array size by 1 */
//         storageIndex--;
//         // Free up the memory
//         free(localStorage[index]);
//         return 0;
//     }
// }

// Condition defines
#define TIMES_UP 0
#define DATA_READY 1
#define DATA_SENDING 2
#define DATA_SEND 3
#define NO_NEW_DATA 4

unsigned int condition = NO_NEW_DATA;

// UART buffers
unsigned char read_buff[1];
unsigned char store_buff[3] = {'\0', '\0','\0'};

// UART init
#define MY_SERIAL DT_NODELABEL(uart0)
#if DT_NODE_HAS_STATUS(MY_SERIAL, okay)
#else
#error "Node is disabled"
#endif

bool cloud_connected = true;

struct k_timer uart_timer;

// Timer if there is no response on uart
void no_response(struct k_timer *timer_id) {
    condition = TIMES_UP;
    cloud_connected = false;
    k_timer_stop(&uart_timer);
}

static struct k_timer check_UART;
void print_nd()
{
    printk("nd");
}

// Sends the local storage to the cloud
int send_to_cloud() {
    int ret = -1;

    // Initialize UART
    const struct device *uart_dev;
    uart_dev = device_get_binding(DT_LABEL(MY_SERIAL));
    k_timer_init(&check_UART, print_nd, NULL);
    // Create timer in case there is no response
    struct k_timer uart_timer;
    // k_timer_init(&uart_timer, no_response, NULL);

    for(int i = 0; i<storageIndex;i++)
    {
        printk("[%d] Indx=%d\n",localStorage[i].time,storageIndex);

         // Signal new data and change condition
        printk("nd\n");
        condition = DATA_READY;

        // Try as long as needed till data has been received
        while (condition != DATA_SEND || condition == TIMES_UP) {
            
            // Read uart
            if (uart_poll_in(uart_dev, read_buff) == 0) {
                store_buff[0] = store_buff[1];
                store_buff[1] = *read_buff;
                printk("%s\n", store_buff);
            }
            // Check if there is a response
            if (condition == DATA_READY) {
                
                if (!strcmp(store_buff, "ok")) {
                    k_timer_stop(&check_UART);
                    condition = DATA_SENDING;
                    // k_timer_start(&uart_timer, K_SECONDS(1), K_SECONDS(1));
                    printk("{\"time\": %d, \"temp\": %d, \"humi\": %d, \"pres\": %d, \"batt\": %d, \"airf\": %d, \"test\": %d}\n", 
                                localStorage[i].time, localStorage[i].temp, 
                                localStorage[i].humi, localStorage[i].pres,
                                localStorage[i].batt, localStorage[i].airf, localStorage[i].test);

                }
                else
                    k_timer_start(&check_UART, K_MSEC(10), K_MSEC(100));
            }
            // Check if data has been received ("dn" -> done)
            else if (condition == DATA_SENDING && !strcmp(store_buff, "dn")) {
                condition = DATA_SEND;
                cloud_connected = true;
            }
            // Send data again when not received
            else if (condition == DATA_SENDING) {
                // k_timer_start(&uart_timer, K_SECONDS(1), K_SECONDS(1));
                // printk("{\"time\": %d, \"temp\": %d, \"humi\": %d, \"pres\": %d, \"batt\": %d, \"airf\": %d, \"test\": %d, ", 
                //                 localStorage[i].time, localStorage[i].temp, 
                //                 localStorage[i].humi, localStorage[i].pres,
                //                 localStorage[i].batt, localStorage[i].airf, localStorage[i].test);
            }
        }       
        
        if(cloud_connected)
        {
            //Send to cloud if succes remove it
            if (!remove_sensor_series(i))
            {
                printk("Succes, SI:%d\n",storageIndex);
                cloud_connected = !cloud_connected;
                ret =  0;
            }
            condition = NO_NEW_DATA;
        }
        else {
            printk("Cloud not connected, Ind=%d\n",storageIndex);
            // cloud_connected = !cloud_connected;
        }
    }
    
    return ret;
}

// Stores the data into the airflow struc
airflow_local store_payload(airflow_local data, uint16_t* payload)
{
    data.time = (uint32_t) k_uptime_ticks();;
    data.temp = payload[1];
    data.humi = payload[3];
    data.pres = payload[5];
    data.batt = payload[7];
    data.airf = payload[9];
    data.test = payload[11];
    printk("Data: %d | %d | %d | %d | %d | %d | %d \n", data.time, data.temp, data.humi, data.pres, data.batt, data.airf, data.test);
    return data;
}

//Print all storages
void print_storage_all(){
    if( storageIndex == 0)
    {
        printk("Storage empty\n");
        return;
    }
    for(int i = 0;i<storageIndex;i++)
        print_storage(i);
}
//Print index storage
void print_storage(int index){
    airflow_local data;
    data = localStorage[index];
    printk("Data: %d | %d | %d | %d | %d | %d | %d \n", data.time, data.temp, data.humi, data.pres, data.batt, data.airf, data.test);
}
