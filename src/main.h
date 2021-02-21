#include <Arduino.h>

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
	<head>
			<title>Steam-Hat</title>
			<meta http-equiv="Content-type" content="text/html; charset=utf-8"/>
    	<meta name="viewport" content="width=device-width,initial-scale=1">
	</head>
	<body>
		<p>Battery voltage: <span class="voltage">unknown</span></p>
		<a href="">Byggbeskrivning</a>
		<script>
			function async renderStatus() {
   			const response = await fetch('/status', {
    			cache: 'no-cache'
  			});

  			const status = response.json();
				document.querySelector(".voltage").text(status.voltage);
			}

			setInterval(() => {
				renderStatus();
			}, 2000);
		</script>
	</body>
</html>
)=====";