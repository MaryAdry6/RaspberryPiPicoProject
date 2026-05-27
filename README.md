# Pedală Bass Titicaca

## Echipa și Obiective
### Membrii echipei:
* Ciurilă Maria-Adriana
* Frandeș Eugen-Codrin
* Pănescu Andrei

### Obiectivele proiectului:
Obiectivul principal este realizarea unui sistem digital de procesare a semnalului audio în timp real, implementat pe platforma Raspberry Pi Pico 2 WH. Dispozitivul va capta semnalul analogic emis de o chitară bass, aplică algoritmi DSP selectați de utilizator (Distortion,Tremolo, Delay) și transmite sunetul procesat în timp util către un amplificator audio, având posibilitatea de monitorizare și control printr-o interfață web integrată.

### Componente Hardware:
* *Microcontroler*: Raspberry Pi Pico 2 WH;
* *Intrare/Ieșire Audio*: **ADC**-ul intern al plăcuței;
* *Comenzi Fizice*: Toggle Switch pentru Power On/Off;
* *Indicatori (LED)*: **Alb** indică Power Status-ul, **Roșu** indică Distortion activ, **Albastru** indică Tremolo activ, **Galben** indică Delay activ;
* *Conectivitate*: 2x **Jack 6.35mm (1/4")** pentru input-ul de la bass output-ul către amplificator, **portul Micro-USB** (5V) pentru alimentare, **Wi-Fi** integrat pentru comunicarea cu interfața web.

## Cerințe Funcționale și Non-Funcționale
### Cerințe Funcționale:
*CF1. Procesare Audio Continuă* - Sistemul preia semnalul analogic prin mufa Jack Input, îl procesează în timp real și îl retransmite prin mufa Jack Output.
*CF2. Semnalizare Vizuală* - Sistemul dispune de un ansamblu de 4 LED-uri pentru indicarea stării curente:
- **LED Alb:** Efect activat pe modul **Clean** (semnal de bază, fără procesare acustică adițională).
- **LED Roșu:** Efect **Distortion** activat.
- **LED Albastru:** Efect **Reverb** activat.
- **LED Galben:** Efect **Delay** activat.

*CF3. Interfață Web* - Modificarea algoritmului de efect activat (Distortion / Tremolo / Delay) se realizează exclusiv utilizând interfața grafică Web UI.
*CF4. Sincronizare Vizuală HW-SW* - Orice schimbare de stare efectuată în interfața Web UI determină stingerea automată a LED-ului precedent și aprinderea LED-ului corespunzător noii selecții pe hardware-ul fizic.

### Cerințe Non-Funcționale:

*CNF1. Latență* - Întârzierea generală a procesării audio (de la ADC Input la PWM Output) nu trebuie să depășească 5-10 ms, fiind insesizabilă pentru muzician în timpul execuției live.

*CNF2. Fiabilitate și Prioritizare Execuție* - Serverul web și interfața Wi-Fi nu au voie să blocheze sau să întrerupă firul principal de execuție dedicat eșantionării și procesării audio.

*CNF3. Eficiență Resurse* - Utilizarea perifericului hardware DMA (Direct Memory Access) pentru transferul direct de date între periferice (ADC/PWM) și memorie, minimizând încărcarea nucleelor procesorului.

*CNF4. Ergonomie și UI Intuitiv* - Elementele vizuale din interfața Web UI folosesc palete de culori identice cu culorile fizice ale LED-urilor asociate (Roșu, Albastru, Galben, Alb).

*CNF5. Siguranță Electronică* - Dimensionarea rezistențelor de limitare atașate LED-urilor pentru a asigura un consum de curent situat sub pragul maxim recomandat per pin GPIO de pe RP2350.

## Scenariu de Testare:
### Verificarea Integrității Semnalului Analogic

##### Obiectiv: Validarea fluxului audio complet, funcționarea corectă a modului _Clean_ și sincronizarea vizuală la trimiterea cererilor de la distanță.

|**Pas**|**Acțiune Utilizator**|**Rezultat Așteptat**|
|-|-|-|
|1|Se conectează bass-ul la mufa de intrare și un amplificator la cea de ieșire. Se introduce cablul Micro-USB și se pornește Toggle Switch-ul.|Sistemul pornește corect. LED-ul Alb se aprinde indicând modul inițial "Clean". Semnalul audio nativ se aude în amplificator clar, fără distorsiuni majore sau zgomot de fond indus.|
|2|Utilizatorul se conectează la rețeaua Wi-Fi a pedalei și accesează adresa IP a serverului web în browser.|Interfața Web UI se încarcă complet, indicând vizual corelația cu modul curent.|
|3|Se selectează efectul „Distortion” din interfața Web UI.|LED-ul Alb se stinge instantaneu, LED-ul Roșu se aprinde, iar semnalul din amplificator capătă caracteristicile acustice de distors/saturație.|

## Diagramă de Componente / Schemă Bloc
<img width="952" height="741" alt="schema_bloc_pedalabass" src="https://github.tuiasi.ro/user-attachments/assets/b86ac664-9c54-45e5-94db-0dc5b083856b" />

## Parametrii Relevanți ai Componentelor și Integrarea Lor

Integrarea componentelor în arhitectura propusă se bazează pe potrivirea parametrilor electrici și dinamici dintre etajele analogice și cele digitale ale microcontrolerului RP2350.

### Rata de Eșantionare și Rezoluția ADC-ului:
*Parametri* - Rezoluție de 12 biți (valori între 0 și 4095), rată de eșantionare fixată la 50 kHz.

*Argumentare* - Conform teoremei Nyquist-Shannon, pentru a reproduce un semnal audio de bass cu o bandă utilă de până la 20kH este necesară o frecvență de eșantionare de cel puțin dublul acestei valori. Alegerea valorii de 50 kHz oferă o rezoluție temporală excelentă de 20 μs per eșantion, reducând la minimum zgomotul de cuantizare.

### Configurația Modulului PWM ca DAC:
*Parametri* - Frecvență de ceas a sistemului de $150MHz, valoare de "wrap" setată optim pentru a asigura o frecvență a purtătoarei PWM mult peste banda audio (de ordinul sutelor de kiloherți).

*Argumentare* - Renunțarea la un DAC extern pe I2C (cum era inițial MCP4725) reprezintă o optimizare critică. Transmisia I2C la 400 kHz sau 1 MHz introducea blocaje și timpi mari de așteptare. Generarea semnalului prin modulul PWM intern controlat direct prin regiștri hardware permite scrierea asincronă instantanee, scăzând latența audio de procesare la valori apropiate de zero.

### Circuitul de Adaptare a Semnalului de Intrare:
*Parametri* - Circuit divizor rezistiv cu condensator de decuplare pentru a crea o tensiune de polarizare (bias) la 1.65 V.

*Argumentare* - Deoarece semnalul audio alternativ de la instrument are oscilații negative și pozitive, iar ADC-ul intern al plăcuței Pico 2 acceptă doar intrări în domeniul [0 V, 3.3 V], adăugarea componentei de DC (offset) la mijlocul intervalului (1.65 V) elimină riscul de tăiere (clipping) a semnalului.

#### Referințe Documentație Tehnică:

 - *RP2350 Datasheet (Raspberry Pi Ltd.)*
 - *Hardware Design with RP2350*
 - Cursuri și Laboratoare

## Schemă Electrică și Testarea HW-SW
### Schema Electrică
<img width="2480" height="3508" alt="schema electrica" src="https://github.tuiasi.ro/user-attachments/assets/bd9bf50b-e9c7-41fa-a40f-b2b7997b148a" />

### Descrierea Procesului de Testare și Integrare HW-SW
Integrarea componentelor software cu cele hardware se realizează în pași incrementali pentru a facilita depanarea:

 1. **Testare Hardware Nivel Zero:** Verificarea cu multimetrul a prezenței tensiunii de 3.3 V pe pinii Pico după acționarea switch-ului și măsurarea punctului de bias pe pinul ADC (1.65 V fără instrument conectat).
 2. **Integrare și Testare Driver Periferice:** Încărcarea unui cod minimalist care citește eșantionul din ADC și îl scrie direct ca factor de umplere în registrul PWM, ocolind bufferele și rețeaua. Scopul este calibrarea filtrului acustic de la ieșire și verificarea absenței distorsiunilor hardware.
 3. **Integrare și Validare Conectivitate Wi-Fi + Server Web:** Pornirea modulului Wi-Fi pe Core 0. Se rulează un script de test care simulează schimbări rapide ale stării din interfață pentru a garanta că rutările software aprind LED-urile fizice corecte fără a afecta stabilitatea microcontrolerului.

## Schemă Bloc Software
<img width="691" height="1561" alt="schema_bloc_software_v2" src="https://github.tuiasi.ro/user-attachments/assets/e853276a-ae2b-44c8-81c2-56cf2a01c106" />


## Documentare Foto și Instrucțiuni de Utilizare
### Proiectul Complet
![foto1](https://github.tuiasi.ro/user-attachments/assets/92049fac-a6dd-4732-88c5-68823867a30a)
### Prima conectare
Odată ce plăcuța este conectată la curent, utilizatorul trebuie să se conecteze la adresa Wi-Fi a acesteia:
#### Denumire: PicoBassPedal
#### Parola: bass1234
<img width="361" height="187" alt="wifi0" src="https://github.tuiasi.ro/user-attachments/assets/5149ae60-4a84-4f74-bc41-de372d178846" />

La prima conectare, utilizatorul poate vedea rețeaua plăcuței în setările Wi-Fi ale dispozitivului, dar după conectare este întâmpinat de această eroare la accesarea interfeței web:
<img width="1867" height="1035" alt="wifi1" src="https://github.tuiasi.ro/user-attachments/assets/c3facae4-06fa-4bcd-8062-bf1dbb0f602a" />
Utilizatorul trebuie să intre în setările rețelei,
<img width="1226" height="1026" alt="wifi2" src="https://github.tuiasi.ro/user-attachments/assets/79a55514-6c7f-4cf6-a2c7-abb3b9a0cfd3" />
să modifice setarea *"IP Assignment"* din automatic în **Manual**
<img width="1088" height="58" alt="wifi3" src="https://github.tuiasi.ro/user-attachments/assets/f5905939-6593-4f66-8bde-bc3676e21447" />

<img width="582" height="255" alt="wifi4" src="https://github.tuiasi.ro/user-attachments/assets/df00b881-0a6a-4d7a-bc3f-01c21dbbf81a" />

și să completeze adresele cerute astfel:

<img width="579" height="862" alt="wifi5" src="https://github.tuiasi.ro/user-attachments/assets/7907d5e5-aff2-4a66-ac21-640cb96cbb8c" />

#### NU UITAȚI SĂ APĂSAȚI BUTONUL DE SAVE!
După, pentru accesarea interfeței web, trebuie introdusă în bara de căutari adresa:

#### 192.168.4.1
<img width="1867" height="1028" alt="wifi6" src="https://github.tuiasi.ro/user-attachments/assets/40953baa-3533-4eb7-83cf-f71666715a67" />

### Cablarea Chitarei Bass
Pentru conectarea bass-ului, pașii sunt următorii:

 1. Conectarea unui cablu jack 6.35 mm între **bass** și **JackIn** al proiectului
![conect1](https://github.tuiasi.ro/user-attachments/assets/3edbeff6-c73c-49b2-a6ee-6a57750ce440)

 2. Conectarea unui cablu jack 6.35 mm între **JackOut** al proiectului și **amplificator**
![conect2](https://github.tuiasi.ro/user-attachments/assets/b35d2685-a348-44fb-a1ff-1032657b7add)

 3. Pornirea amplificatorului
![conect3](https://github.tuiasi.ro/user-attachments/assets/b6065834-056c-4504-9b50-5f8c40cee146)

 4. Alimentarea plăcuței Pico prin **MicroUSB**
 5. Ultimul pas, dar cel mai important, pornirea **switch-ului**
![conect4](https://github.tuiasi.ro/user-attachments/assets/bfec22a5-784b-4975-9290-2f0e2f8298fe)

### Folosirea interfeței
Site-ul web nu conține doar panoul de control al pedalei, ci și informații despre fiecare efect implementat.
<img width="530" height="283" alt="web1" src="https://github.tuiasi.ro/user-attachments/assets/d0571c3e-6040-4a9a-aea6-31f869768a4a" />

La secțiunea *"Control"* sunt butoanele de activare a efectelor:
 - Butonul Alb pentru semnalul *"Curat/Clean"* fără vreun efect aplicat
![web2](https://github.tuiasi.ro/user-attachments/assets/aaedcfcf-ade2-441e-bf37-24c6746ab4cf)
 - Butonul Roșu pentru Distorsiune cu potențiometru de intensitate al efectului
![web3](https://github.tuiasi.ro/user-attachments/assets/7b1343bf-a8a3-445f-8345-f72ce4e39f3d)
 - Butonul Albastru pentru Tremolo cu potențiometru de intensitate al efectului
![web4](https://github.tuiasi.ro/user-attachments/assets/89b86446-4c78-4fc8-a09e-a25ac5bd4055)
 - Butonul Galben pentru Întârziere
![web5](https://github.tuiasi.ro/user-attachments/assets/99af39f1-12e5-49b1-82c8-90901502c480)


