#include "AirQualitySensor.h"
#include "Utilities.h"

//
// Sensor Type = Panasonic SN-GCJA5
//   https://na.industrial.panasonic.com/products/sensors/air-quality-gas-flow-sensors/lineup/laser-type-pm-sensor/series/123557/model/123559
//

#define AQM_BUFFER_SIZE 32
#define AQMSerial Serial1

AirQualitySensor::AirQualitySensor()
    :   _pm1p0(0),
        _pm2p5(0),
        _pm10(0),
        _particleCount0p5um(0),
        _particleCount1p0um(0),
        _particleCount2p5um(0),
        _particleCount5p0um(0),
        _particleCount7p5um(0),
        _particleCount10um(0),
       _sensorStatus(0),
       _pm2p5_history(),
       _pm2p5_history_insertion_idx(0)
{


}

AirQualitySensor::~AirQualitySensor()
{

}

void AirQualitySensor::begin(void)
{
    // start hardware serial. RX is pin 33 on TinyPico. Don't really need TX.
    AQMSerial.begin(9600, SERIAL_8E1, 33, 32 );

    // The Panasonic SN-GCJA5 takes 28 seconds to get power up and normalize.
    // we will wait 28 seconds here.
    Serial.println(F("Waiting 28 seconds for sensor to power up and initialize"));
    delay(28000);
}

bool AirQualitySensor::updateSensorReading(void)
{
    uint8_t buffer[AQM_BUFFER_SIZE];
    int recieveCount = 0;
    while( AQMSerial.available() && (recieveCount < AQM_BUFFER_SIZE) ) {
        buffer[recieveCount] = AQMSerial.read();
        recieveCount++; 
    }
    if (recieveCount != AQM_BUFFER_SIZE) {
        Serial.print(F("ERROR: did not receive full data set from sensor. Bytes recieved = "));
        Serial.print(recieveCount);
        Serial.print(F("\n"));
        return false;
    }

    if ((buffer[0] != 0x02) || (buffer[AQM_BUFFER_SIZE-1] != 0x03)) {
        Serial.println(F("ERROR: data received from sensor did not have proper start or stop byte."));
        Serial.print(F("    Stary byte = 0x"));
        Serial.print(buffer[0], HEX);
        Serial.print(F(", stop byte = 0x"));
        Serial.print(buffer[AQM_BUFFER_SIZE-1], HEX);
        Serial.print(F("\n    Flushing input buffer "));
        // This error likely occurs because we got out of synch with the sensor's internal update cycle.
        // We will read more bytes in order to slowly get into the right phase with the sensor.
        while (AQMSerial.available()) {
            AQMSerial.read();
            Serial.print(F("."));
        }
        Serial.print(F("\n"));
        return false;
    }

    // TODO confirm the XOR byte to ensure no transmission errors.
    
    Serial.print(F("    Received data = "));
    print_buffer(buffer, recieveCount);

    // calculate values
    //
    // The English documentation for sensor communications is foound here:
    //      https://b2b-api.panasonic.eu/file_stream/pids/fileversion/8814
    // it is very confusing and clearly not written by someone who speaks  English. What is unclear
    // is that the I2C and UART interfaces actually provide numbers that are formatted differently.
    // Using Google translate on the Japanse version of the document yields a much better translation.
    //      https://industrial.panasonic.com/content/data/PPL/PDF/JA5-SSP-COMM-v10_Communication-Spec_j.pdf
    // In that translation, it becomes clearer that the mass density measurements are scaled by 
    // 1000 in the I2C interface, and NOT sclaed by 1000 in the UART interface. Furthermore, despite
    // the UART interface providing 4 bytes for the mass densities, the number provided is in fact a
    // 16 bit integer. I realize that the English document says something to that extent, but the sentence
    // was extremely confusing. Triangulating between the Google translated Japanese document and the 
    // official English document yielded better insights into what is actuaslly happening.
    //
    // Despite the mass densities only being uint16_t integers in the UART interface, I still calculate them
    // as if they are uint32_t since 4 bytes are provided.
    _pm1p0 = ((uint32_t)buffer[4])*256*256*256 + ((uint32_t)buffer[3])*256*256 + ((uint32_t)buffer[2])*256 + buffer[1];
    _pm2p5 = ((uint32_t)buffer[8])*256*256*256 + ((uint32_t)buffer[7])*256*256 + ((uint32_t)buffer[6])*256 + buffer[5];
    _pm10 = ((uint32_t)buffer[12])*256*256*256 + ((uint32_t)buffer[11])*256*256 + ((uint32_t)buffer[10])*256 + buffer[9];
    _particleCount0p5um = (uint16_t)buffer[14]*256 + buffer[13];
    _particleCount1p0um = (uint16_t)buffer[16]*256 + buffer[15];
    _particleCount2p5um = (uint16_t)buffer[18]*256 + buffer[17];
    _particleCount5p0um = (uint16_t)buffer[22]*256 + buffer[21];
    _particleCount7p5um = (uint16_t)buffer[24]*256 + buffer[23];
    _particleCount10um = (uint16_t)buffer[26]*256 + buffer[25];

    _sensorStatus = buffer[29];

    if (_pm2p5_history.size() < _pm2p5_history.max_size()) {
        _pm2p5_history.push_back(_pm2p5);
        _pm2p5_history_insertion_idx = _pm2p5_history.size() - 1;
    } else {
        // make _pm2p5_history behave like a FIFO stack, but
        // we really don't care about the stack order, so do 
        // so in a compute optimized manner.
        _pm2p5_history_insertion_idx++;
        if (_pm2p5_history_insertion_idx >= _pm2p5_history.size()) {
            _pm2p5_history_insertion_idx = 0;
        }
        _pm2p5_history[_pm2p5_history_insertion_idx] = _pm2p5;
    }

    Serial.print(F("    PM1.0 = "));
    Serial.print(_pm1p0);
    Serial.print(F(", PM2.5 = "));
    Serial.print(_pm2p5);
    Serial.print(F(", PM10 = "));
    Serial.print(_pm10);
    Serial.print(F("\n"));

    return true;
}

uint8_t AirQualitySensor::statusParticleDetector(void) const
{
    return (_sensorStatus&0x30) >> 4;
}

uint8_t AirQualitySensor::statusLaser(void) const
{
    return (_sensorStatus&0x0C) >> 2;
}

uint8_t AirQualitySensor::statusFan(void) const
{
    return (_sensorStatus&0x03);
}

float AirQualitySensor::averagePM2p5( int32_t window_size_seconds ) const
{
    // this averaging makes the grand assumption that all measurements suceed. That is, it 
    // does not account for measurement holes caused by intermitent sensor failures. For the
    // purposes of what this value is used for, which is to determine when the AQI is based on 
    // a 24 hour average, thiscaveat isn't really that important. Just acknowledging it exists.

    size_t curr_idx = _pm2p5_history_insertion_idx;
    size_t value_count = 0;
    uint32_t pm2p5_sum = 0;
    
    while (((value_count*AIR_QUALITY_SENSOR_UPDATE_SECONDS) < window_size_seconds) && (value_count < _pm2p5_history.size())) {
        pm2p5_sum += _pm2p5_history.at(curr_idx);
        value_count++;
        if (curr_idx == 0) {
            curr_idx = _pm2p5_history.size() - 1;
        } else {
            curr_idx--;
        }
    }

    return (float)pm2p5_sum/(float)value_count;
}

float AirQualitySensor::airQualityIndex( float avgPM2p5 ) const
{
    // 
    // Calculate teh AQI. Got this formula from:
    //   https://www.epa.gov/sites/production/files/2016-04/documents/2012_aqi_factsheet.pdf
    //

    float lowPM2p5, highPM2p5, lowAQI, highAQI;

    if (avgPM2p5 <= 12.0) {
        lowPM2p5 = 0;
        highPM2p5 = 12;
        lowAQI = 0;
        highAQI = 50;
    } else if (avgPM2p5 <= 35.4) {
        lowPM2p5 = 12;
        highPM2p5 = 35.4;
        lowAQI = 50;
        highAQI = 100;
    } else if (avgPM2p5 <= 55.4) {
        lowPM2p5 = 35.4;
        highPM2p5 = 55.4;
        lowAQI = 100;
        highAQI = 150;
    } else if (avgPM2p5 <= 150.4) {
        lowPM2p5 = 55.4;
        highPM2p5 = 150.4;
        lowAQI = 150;
        highAQI = 200;
    } else if (avgPM2p5 <= 250.4) {
        lowPM2p5 = 150.4;
        highPM2p5 = 250.4;
        lowAQI = 200;
        highAQI = 300;
    } else if (avgPM2p5 <= 350.4) {
        lowPM2p5 = 250.4;
        highPM2p5 = 350.4;
        lowAQI = 300;
        highAQI = 400;
    } else {
        // the provided formula has an upper bound of PM2.5 = 500 for a max
        // AQI of 500. This approach allows for extrapolation beyond an
        // AQI of 500.
        lowPM2p5 = 350.4;
        highPM2p5 = 500;
        lowAQI = 400;
        highAQI = 500;
    }

    return lowAQI + (highAQI - lowAQI)*(avgPM2p5 - lowPM2p5)/(highPM2p5 - lowPM2p5);
}