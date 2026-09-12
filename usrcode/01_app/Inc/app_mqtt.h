/**
 ******************************************************************************
 * @file    app_mqtt.h
 * @brief   MQTT ��������ͷ�ļ� �� ESP8266 + Paho MQTT ���ӻ�Ϊ�� IoTDA
 *
 *  �X�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�[
 *  �U  �� �����޸���������Ϊ��Ļ�Ϊ��ʵ�ʲ����������޷����� ��              �U
 *  �d�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�g
 *  �U  1. ��¼��Ϊ�� �� �豸���� IoTDA �� ������Ʒ(Э��MQTT,����JSON)       �U
 *  �U  2. ��Ʒ�����ܶ������ӷ���(Sensor)���������(temperature/humidity)  �U
 *  �U  3. ��Ʒ�����ܶ�����������(LED_Control,����led int)                 �U
 *  �U  4. �豸������豸������ device_id �� device_secret                  �U
 *  �U  5. ������������Ϣ������ MQTT �����ַ                                �U
 *  �U  6. �����߹������ɼ�Ȩ����:                                          �U
 *  �U     https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/      �U
 *  �U     ѡ"��У��ʱ���",���� device_id + device_secret                  �U
 *  �U  7. �����ɵ� ClientId/Username/Password �����·�                     �U
 *  �^�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�a
 *
 *  ��Ϊ�����⣺
 *    �ϱ�����(����): $oc/devices/{device_id}/sys/properties/report
 *    ��������(����): $oc/devices/{device_id}/sys/commands/#
 *    ������Ӧ(����): $oc/devices/{device_id}/sys/commands/response/request_id={rid}
 *
 *  �������̣�
 *    1. AT���� �� ��STAģʽ �� ��WiFi �� TCP����Broker �� MQTT CONNECT �� ������������
 *    2. ���ڷ������������ݣ���Ϊ����ģ��JSON��ʽ��
 *    3. MQTTYield ά������ + ������ƽ̨�����������JSON������LED���ظ���Ӧ��
 *    4. �κλ���ʧ�� �� �ӳ�3�� �� ��ͷ����
 *
 *  MQTT����ΪΨһ�������񣬶�ռ ESP8266
 ******************************************************************************
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>
/* ==========================================================================
 *  �� �û������� ���� ����ݻ�Ϊ��ʵ�ʻ����޸� ��
 * ========================================================================== */

/* --- WiFi --- */
// ������һ�����ϻ�������wifi �ȵ� ����Ҫ��Ϊ�˿�����ͨwifiģ�飬�����ϻ�Ϊ�ƣ�
#define MQTT_WIFI_SSID        "your_wifi_ssid"        /* !< 2.4GHz WiFi ���� */
#define MQTT_WIFI_PASSWORD    "your_wifi_password"    /*!< WiFi ���� */

//----------------------------

/* --- ��Ϊ�� IoTDA ����������Ѵ��豸������Կ�ļ����룩 --- */
/* ע�⣺��Կ�ļ������� MQTTS(8883/TLS)��ESP8266 AT�����÷Ǽ��� MQTT(1883)��
   ��Ϊ�ƹٷ�˵�����Ǽ��ܽ����������䣬�����˿� 8883 ��Ϊ 1883 ���ɣ�
   ��Ҫ�� iot-mqtts �ĳ� iot-mqtt�������������ڣ��ᵼ��DNS����ʧ�ܣ� */
#define MQTT_BROKER_HOST      "6e7a274047.st1.iotda-device.cn-north-4.myhuaweicloud.com"  /*!< �Ǽ���MQTT�����ַ��������MQTTS��ͬ�� */
/* ��ѡ��IPv4 ֱ�����ס����豸ר������ͬʱ���� IPv6(AAAA) ��¼������ ESP8266 AT �̼�
   �������������ᳬʱ������WiFi ���Ϻ� TCP connect FAILED ѭ�����ԣ���
   ����Ϊ�������� IPv4 A ��¼��������ģ�� DNS ֱ�ӽ�����2026-09 ��֤ A ��¼=117.78.5.125����
   ���Ϊ�Ƶ�����ַ���������ϣ����½����������´�ֵ����ע�͵���иĻ�����ֱ���� */
#define MQTT_BROKER_IP        "117.78.5.125"
#define MQTT_BROKER_PORT      1883U             /*!< MQTT �Ǽ��ܶ˿� */
#define MQTT_CLIENT_ID        "your_client_id"  /*!< ������Կ�ļ��е�clientId */
#define MQTT_USERNAME         "your_username"    /*!< ������Կ�ļ��е�username����device_id�� */
#define MQTT_PASSWORD         "your_password"  /*!< ������Կ�ļ��е�password */
#define MQTT_KEEPALIVE_SEC    60U                /*!< ���������룩 */

/* --- ��Ϊ���豸ID����������ƴ�ӣ���MQTT_USERNAME��ͬ�� --- */
#define HUAWEI_DEVICE_ID      "your_device_id"    /*!< �豸ID */

/* --- ��Ϊ����ģ�ͷ���ID�����������̨"���ܶ���"�еķ���IDһ�£� --- */
#define HUAWEI_SERVICE_ID     "Sensor"           /*!< ��ģ�ͷ���ID */

/* --- ���⣨��Ϊ�ƹ̶���ʽ��device_id ����ʱƴ�ӣ� --- */
#define MQTT_QOS              0                    /*!< QoS�ȼ� 0/1/2 */
#define MQTT_OFFLINE_CACHE_MAX  16U                /*!< ���߻��������Ϣ����RAM���λ��壩 */

/* --- ���в��� --- */
#define MQTT_PUBLISH_PERIOD_MS  3000U   /*!< �����ϱ����ڣ����룩 */
#define MQTT_RECONNECT_DELAY_MS 3000U   /*!< ���ݱ����ʵ�������ȴ�����ָ���˱ܣ�1000ms ������30s������ app_mqtt.c */
#define MQTT_YIELD_TIMEOUT_MS   200     /*!< ÿ��MQTTYield�ĳ�ʱ�����룩��ԽС������ӦԽ�� */

/* ==========================================================================
 *  �������
 * ========================================================================== */
#define MQTT_TASK_STACK_SIZE   (1024U * 4U)     /*!< ����ջ��С���ֽڣ���MQTT��ϴ�ջ */
#define MQTT_TASK_PRIORITY     osPriorityNormal /*!< �������ȼ� */

/* ==========================================================================
 *  API
 * ========================================================================== */

/**
 * @brief  MQTT �����������
 * @param  argument δʹ��
 * @note   �� app_tasks.c ����Ϊ FreeRTOS ����
 */
void APP_MQTT_Task(void *argument);

/**
 * @brief  ��ȡMQTT����״̬
 * @retval bit0=WiFi������, bit1=TCP������, bit2=MQTT������
 */
uint8_t APP_MQTT_GetStatus(void);

/**
 * @brief  �ⲿ�����ӿڣ���������ɵ��ã��̰߳�ȫ��
 * @param  payload  �������ݣ���Ϊ�������ϱ�JSON��ʽ��������MQTT�غɣ�
 * @param  len      ���ݳ���
 * @retval 0=����ӣ�MQTT���񽫷���/���߻��棩, 1=��������, 3=�����������÷����л��壩
 * @note   ���ݾ��̰߳�ȫ���н��� MQTT ���񷢲�ͨ��������ʧ���Զ�ת�����߻����ش���
 *         �� svc_conn ���棨ң��/Ӱ���ϱ������ϲ����
 */
uint8_t APP_MQTT_Publish(const uint8_t *payload, uint16_t len);

/**
 * @brief  �ⲿ�����ӿڣ�ָ�����������
 * @param  topic  ���������"report"=�����ϱ�, "messages/up"=�豸��Ϣ���У�NULL�ȼ�"report"��
 * @param  payload  ��������
 * @param  len      ���ݳ���
 * @retval ͬ APP_MQTT_Publish
 */
uint8_t APP_MQTT_PublishTopic(const char *topic, const uint8_t *payload, uint16_t len);

#endif /* APP_MQTT_H */
