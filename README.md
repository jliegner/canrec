# canrec
Das ist ein simpler CAN to SD-Card Recorder den ich verwende um die BionX CAN Kommunikation mitzuschneiden. Als Hardware kommt ein OLIMEXINO STM32 zum Einsatz.
Das Board hat bereits alle nötige Hardware so dass ausser der Spannungsversorgung nur noch die CAN-Leitungen zu verbinden sind. 
https://www.olimex.com/Products/Duino/STM32/OLIMEXINO-STM32/open-source-hardware

Die Baudrate ist fest auf 125kb eingestellt.
Nach dem Einschalten blinkt die gelbe LED kurz. Der Mitschnitt ist deaktiviert. Kurzer Druck auf den boot0 Butten aktiviert den Mitschnitt. Dabei geht die grüne LED an und die gelbe LED blinkt kurz wenn eine CAN-Msg empfangen wurde. 
Sollte die grüne LED mehrfach blinken liegt ein Problem mit der SD-Karte vor oder es ist keine eingesteckt.
Erneutes Drücken von boot0 beendet die Aufnahme. Die Dateien auf der SD-Karte werden fortlaufend mit Nummern bezeichnet. 

Es wird standardmäßig im Silent-Modus gearbeitet. Mit einer Brücke am EXT-Steckverbinder von Pin15 zu Pin16 kann aber auch im Normal-Modus gearbeitet werden.






