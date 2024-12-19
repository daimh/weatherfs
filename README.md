# weatherfs: realtime weather info by zipcode in a FUSE filesystem

Weatherfs mounts a FUSE filesystem, creates a file for each zipcode specified
in the weatherfs.json conf file. New zipcode can also be added with command 'touch'.

Below is an example of Hawaii current weather.

```
$ cat zipcode/96701
{
  "coord": {
    "lon": -157.9332,
    "lat": 21.390799999999999
  },
  "weather": [
    {
      "id": 800,
      "main": "Clear",
      "description": "clear sky",
      "icon": "01n"
    }
  ],
  "base": "stations",
  "main": {
    "temp": 295.13,
    "feels_like": 295.63,
    "temp_min": 294.58999999999997,
    "temp_max": 295.45999999999998,
    "pressure": 1016,
    "humidity": 86,
    "sea_level": 1016,
    "grnd_level": 1007
  },
  "visibility": 10000,
  "wind": {
    "speed": 1.79,
    "deg": 340
  },
  "clouds": {
    "all": 6
  },
  "dt": 1733659415,
  "sys": {
    "type": 2,
    "id": 2039480,
    "country": "US",
    "sunrise": 1733677087,
    "sunset": 1733716207
  },
  "timezone": -36000,
  "id": 5856430,
  "name": "‘Aiea",
  "cod": 200
}
```

### Usage 
- clone this repo

- obtain a free api key from 
[openweathermap.org](https://openweathermap.org/appid).
the freemium plan is good enough, all it needs is an email address.

- set the key in file 'weatherfs.json'

- install development package per your Linux distribution
```
# apt install libfuse3-dev libcurl4-openssl-dev libjansson-dev libjansson4 # Ubuntu
# pacman -S --needed fuse3 curl jansson # Archlinux
```
- compile and run it
```
cmake -B build
cmake --build build 
mkdir zipcode
build/weatherfs zipcode
cat zipcode/96701 # Hawaii
grep -w temp zipcode/99501 # Alaska
touch zipcode/92328 # Death Valley
grep -w humidity zipcode/92328
umount zipcode
```

### Contribution
Welcome and please

### License
the MIT License
