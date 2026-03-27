# Pedală Bass Titicaca





#### Obiectivele proiectului:



Obiectivul principal este realizarea unui sistem digital de procesare a semnalului audio în timp real, implementat pe platforma Raspberry Pi Pico 2 WH. Dispozitivul va capta semnalul audio emis de bass, va aplica unul dintre algoritmii disponibili (Distorsion, Reverb, Delay) și va transmite sunetul procesat către amplificator.





#### Componente Hardware:



* *Microcontroler*: Raspberry Pi Pico 2 WH;
* *Intrare/Ieșire Audio*: **DAC** MCP4725 (12-bit Resolution, I2C Interface), **ADC**-ul intern al plăcuței;
* *Comenzi Fizice*: Toggle Switch pentru Power On/Off, Buton Ninigi PS10B2BK pentru Activare/Dezactivare efect;
* *Indicatori (LED)*: **Alb** indică Power Status-ul, **Roșu** indică Distortion activ, **Albastru** indică Reverb activ, **Galben** indică Delay activ;
* *Conectivitate*: 2x **Jack 6.35mm (1/4")** pentru input-ul de la bass output-ul către amplificator, **portul Micro-USB** (5V) pentru alimentare, Wi-Fi integrat pentru comunicarea cu interfața web.





#### Cerințe Funcționale:



1. LED-ul Alb se aprinde imediat ce switch-ul este pe On.
2. Sistemul trebuie să preia semnalul prin mufa Jack Input și să îl redea prin Jack Output după procesare.
3. Butonul fizic activează/dezactivează efectul selectat.
4. Schimbarea algoritmului (Distors/Reverb/Delay) se face exclusiv prin Web UI.
5. LED-urile colorate (roșu/albastru/galben) trebuie să reflecte efectul ales în interfața web.





#### Cerințe Non-Funcționale:



1. *Latență*: Întârzierea procesării audio (Input-to-Output) trebuie să fie insesizabilă de către muzician.
2. *Fiabilitate*: Web server-ul nu trebuie să blocheze execuția thread-ului de procesare audio (audio processing priority).
3. *Eficiență*: Utilizarea DMA (Direct Memory Access) pentru transferul datelor audio fără a bloca nucleele procesorului.
4. *UI Intuitiv*: Interfața web trebuie să aibă culori care să corespundă LED-urilor fizice.
5. *Consum Energie*: LED-urile trebuie configurate cu rezistențe adecvate pentru a nu depăși curentul maxim per pin GPIO al Pico 2.





#### Scenariu de Testare:



###### Verificarea Integrității Semnalului Analogic

Obiectiv: Verificarea conexiunilor fizice (Jack) și a alimentării USB.



|**Pas**|**Acțiune Utilizator**|**Rezultat Așteptat**|
|-|-|-|
|1|Conectare cabluri|Se conectează bass-ul la mufa de intrare și un amplificator la cea de ieșire. Se introduce cablul Micro-USB.|
|2|Verificare LED Alb|LED Alb aprins, sunetul trece prin pedală în mod "Bypass" fără distorsiuni majore sau brum excesiv.|
|3|Activare efect din Web UI|Depinzând de efectul ales, LED-ul respectiv se aprinde, sunetul este procesat (amplificatorul redă sunetul cu efect)|



