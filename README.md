### Descrizione del progetto
Si intende simulare il traffico di navi cargo per il trasporto di merci di vario tipo, attraverso dei porti. Questo viene
realizzato tramite i seguenti processi
• un processo master incaricato di creare gli altri processi e gestire la simulazione, ove necessario;
• un numero SO_NAVI (≥ 1) di processi nave; e
• un numero SO_PORTI (≥ 4) di processi porto.
Nella descrizione del progetto si farà riferimento a:
• il tempo simulato, ovvero il tempo che trascorre nella simulazione (esempio: un giorno per trasportare una
merce)
• il tempo reale, ovvero il tempo di durata dell’esecuzione della simulazione (esempio: dopo l’avvio, la simulazione
termina dopo 30 secondi, pur avendo simulato una durata di 30 giorni).
Nella simulazione, un giorno del tempo simulato dura un secondo di tempo reale.

### Le merci
Nella simulazione esistono SO_MERCI tipi diversi di merce. Se ritenuto utile, si può identificare ogni tipo di merce
con un nome oppure con un identificativo numerico. Un lotto di ogni tipo di merce è caratterizzato da:
• una quantità di merce (in ton), estratta casualmente fra 1 e SO_SIZE all’inizio della simulazione e
• un tempo di vita (in giorni), estratto casualmente fra SO_MIN_VITA e SO_MAX_VITA, misurato in giorni.
Offerta e domanda di merci vengono generate presso i porti (si veda descrizione in seguito). Ogni volta che una
merce di un certo tipo viene generata a run-time, avrà sempre le stesse caratteristiche di cui sopra.
Quando il tempo di vita di una merce è trascorso, la merce viene persa e svanisce da ovunque essa sia (nave o
porto). Il tempo di vita di una merce è relativo al momento della sua creazione presso un certo porto.
Tutta la merce generata durante la simulazione si classifica in:
• merce presente in un porto (disponibile per il carico)
• merce presente su una nave
• merce consegnata ad un porto che la richiede
• merce scaduta in porto
• merce scaduta in nave

### La mappa
La mappa del mondo è rappresentata da un quadrato di lato SO_LATO di tipo (double), misurata in kilometri
(Terra piatta). Una posizione sulla mappa è rappresentata dalla coppia di coordinate (come su piano cartesiano).
Sia porti che navi hanno una posizione sulla mappa. La navigazione fra due punti della mappa può sempre avvenire
in linea retta: non esistono grandi masse terrestri che richiedono di essere aggirate.

### Il processo nave
Ogni nave ha
• una velocità SO_SPEED misurata in kilometri al giorno (identica per tutte le navi),
• una posizione, ovvero una coppia di coordinate di tipo (double) all’interno della mappa,
• una capacità SO_CAPACITY (in ton) che misura il carico totale trasportabile (identica per tutte le navi). 

Una nave non può trasportare una quantità di merci che ecceda la propria capacità.
Le navi nascono da posizioni casuali e senza merce. Non c’è limite al numero di navi che possono occupare una
certa posizione sulla mappa, mentre il numero di navi che possono svolgere contemporaneamente operazioni di
carico/scarico in un porto è limitato dal numero di banchine.
Lo spostamento della nave da un punto ad un altro è realizzato tramite una nanosleep che simuli un tempo simulato di:
distanza fra posizione di partenza e di arrivo / velocità di navigazione .

Nel calcolare questo tempo, si tenga conto anche della parte frazionaria (per esempio, se si deve dormire per 1.41
secondi non va bene dormire per 1 o 2 secondi). Si ignorino le eventuali collisioni durante il movimento sulla mappa.
Le navi possono sapere tutte le informazioni sui porti: posizione, merci offerte/richieste, etc. Ogni nave, in
maniera autonoma:
• si sposta sulla mappa
• quando si trova nella posizione coincidente con il porto, può decidere se accedere ad una banchina e, se questo
avviene, può decidere lo scarico o il carico della merce ancora non scaduta.
La negoziazione fra nave e porto su tipologia e quantit`a di merci da caricare o scaricare avviene in un momento a
discrezione del progettista (prima di partire per la destinazione, quando arrivata nel porto, altro, etc.)
Le navi non possono comunicare fra loro, n´e sapere il contenuto di quanto trasportato dalle altre navi.

### Il processo porto
Il porto è localizzato in una certa posizione della mappa e gestisce un numero casuale di banchine compreso fra 1 e
SO_BANCHINE. Esistono sempre almeno quattro porti (SO_PORTI≥ 4), uno per ogni angolo della mappa.
Quando i processi porto vengono creati, viene creata casualmente anche domanda e offerta di merci presso di
essi. Sia la domanda che l’offerta totale di tutte le merci presso tutti i porti sono pari a SO_FILL (in ton). Sia
offerta che richiesta di merci sono caratterizzate da
• il tipo di merce
• la quantità di merce.

Non ci può essere sia offerta che richiesta di una stessa merce nello stesso porto. Non appena la merce è stata
creata viene marcata con la propria data di scadenza allo scadere dalla quale, la merce risulta inutilizzabile e viene
dichiarata sprecata.
Le banchine del porto sono gestite come risorse condivise protette da semafori che impediscano l’utilizzo da
parte di un numero di navi maggiore delle banchine presenti. Quando una nave raggiunge un porto (sia per carico che scarico) 
chiede l’accesso ad una banchina. Se lo ottiene, tiene occupata (con nanosleep(...)) la banchina per un tempo pari a:
quantità di merce scambiata (in ton) / velocità carico/scarico (in ton/giorno).

Con “velocità” uguale al parametro SO_LOADSPEED, misurato in ton/giorno. La velocità di carico e scarico è identica.
Quando viene scaricata una merce di cui c’è richiesta, allora tale richiesta viene soddisfatta e conteggiata come tale.

### Dump stato simulazione
Al trascorrere di ogni giorno, deve essere visualizzato un report provvisorio contenente:
• Totale delle merci suddivise per tipologia e stato (disponibile, consegnato, etc.)
• Numero di navi:
– in mare con un carico a bordo,
– in mare senza un carico,
– in porto, facendo operazioni di carico/scarico.
• Per ogni porto si indichi
– la quantità di merce presente, spedita, e ricevuta
– il numero di banchine occupate/totali

### Terminazione della simulazione
La simulazione termina in una delle seguenti circostanze
• dopo un tempo simulato di SO_DAYS
• quando per ogni tipo di merce
– l’offerta è pari a zero oppure
– la richiesta è pari a zero.

Il report finale deve indicare:
• Numero di navi ancora in mare con un carico a bordo
• Numero di navi ancora in mare senza un carico
• Numero di navi che occupano una banchina
• Totale delle merci suddivise per tipologia e stato (disponibile, consegnato, etc. Si veda la Sezione 5.1 per la
descrizione dello stato di una merce)
• Per ogni porto si indichi la quantità di merce
– presente, spedita, e ricevuta.
• Per ogni tipo di merce si indichi:
– la quantità totale generata dall’inizio della simulazione e quanta di essa
∗ è rimasta ferma in porto
∗ è scaduta in porto
∗ è scaduta in nave
∗ è stata consegnata da qualche nave.
– Si indichi il porto che
∗ ha offerto la quantità maggiore della merce
∗ e quello che ha richiesto la quantit`a maggiore di merce.
