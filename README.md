# ECE531ThermostatFinal

Thermostat_reader reads from a web server on AWS EC2 at url http://13.57.204.147:8080.

you can get on the /state and /temps paths

state: gets whether the heater should be on or off
temps: gets the temperature settings for AM, PM and NIGHT

ARM C code POST operations on current temp from tcsimd, then the web server determines based on time if the server should be on or off baesd on temperature ranges
