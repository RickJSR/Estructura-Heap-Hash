//QUASH - Autor: Ricardo de Jesús Sánchez Rodríguez - Cómputo de alto rendimiento 2024
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <time.h>

//Definimos los infinitos (los valores menor y mayor posibles con el tipo INT)
#define INF_P "9223372036854775808"
#define INF_N "-9223372036854775808"

//El presente programa consta de la implementación de estructuras híbridas entre un heap y una tabla hash
//NOTA 1: El tipo size_t facilita el trabajo con variables que solo almacenan valores enteros positivos
//NOTA 2: "static inline" es una forma de declarar una función para que su ejecución sea más rápida

/****************************************TABLAS HASH******************************************************************/
//Definimos macros (cuando el PC compile, YES lo traduce a 1 y NO a 0... no son variables globales)
#define YES 1
#define NO 0
#define VALID 1
#define NOTVALID 0
#define DELETED NOTVALID
#define LAZY_DELETED -1
#define MAX_64 2147483647
#define FULL 2
#define EMPTY 1
#define UP 1
#define DOWN 0
#define LP 1
#define QP 2
#define DH 3

//En este arreglo se contienen los números primos menores a potencias de 2 (hasta 2^16)
const uint32_t HASH_SIZE[] = {23, 127, 251, 509, 1021, 2039, 4093, 8191, 16381, 32749, 65521, 131071, 262139, 524287, 1048573, 2097143, 4194301, 8388593, 16777213, 33554393, 67108859, 134217689, 268435399, 536870909, 1073741789, 2147483647, 4294967291};

//Constante de ADLER
const uint32_t MOD_ADLER = 65521;

//Variable Global para la histéresis
int hist = 0;

//Variable global para guardar la multiplicidad de un elemento del Heap e imprimirlo (se cambiará su valor cada...
//... vez que se imprima un elemento de heap)
size_t multiplicidad = 0; 

/*Función generadora de llaves*/
uint32_t adler32(unsigned char *data, size_t len) {
    uint32_t a = 1, b = 0;
    size_t index;

    //Process each byte of the data in order
    for (index = 0; index < len; ++index){
        a = (a + data[index]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
    return (b << 16) | a; //Aquí se recorre b 16 bits a la izquierda y después cada bit de b se opera OR con el respectivo bit de a
}

/*Estructura tipo record para incluir la longitud de cadena y los bytes de una información (como un stream de datos, con un puntero al inicio y de ahí sabemos la longitud)*/
typedef struct{
    void *bytes;                //El "void" es para que podamos decir que es un puntero de cualquier tipo de datos
    size_t len;                 //Longitud del contenido
}record;

/****************************************HEAP******************************************************************/
/*Estructura de un elemento (nodo) del heap*/
typedef struct {
    record rec;                 //Contenido a guardar en la posición del heap
    size_t mult;                //Contador de multiplicidad
    size_t hash_index;          //Índice de la tabla hash donde se encuentra el elemento
} heap_item;

/*Estructura del tipo heap*/
typedef struct {
    heap_item *array;           //Primera dirección del arreglo de nodos
    size_t cap;                 //Longitud del arreglo que representa al árbol (capacidad de número de nodos)
    size_t index;               //Índice del último nodo válido 
} heap;

/*Algunos prototipos de funciones de heap*/
heap* newHeapCap(size_t cap);
heap* newHeap();
void freeHeap(heap *h);
/**************************************************************************************************************/

/*Tipo de estructura de un elemento de tabla hash... pero añadiendo su localización en el heap correspondiente*/
typedef struct {
    record rec;                 //Contenido a guardar en la posición de la tabla
    size_t len;                 //Longitud en bytes del record
    char status;                //Estado del item (ponemos si está libre, si está sucio, etc...)
    char lazy_deleted;          //Bandera para indicar si hubo o no un elemento borrado en esa posición
    char leapt;                 //Bandera para indicar que un elemento fue "saltado" durante un proceso de búsqueda de lugar disponible
    size_t heap_index;          //Ubicación del elemento en el heap
    uint32_t key;               //La llave del contenido
} hash_item;                    //Nombre

/*Estructura de la tabla hash con el link agregado hacia el heap (es decir, este es el quash)*/
typedef struct{
    hash_item *table;           //Dirección del primer elemento en el arreglo de las cabezas
    size_t index_size;          //Índice del tipo de capacidad (arreglo de diferentes tamaños con números impares)
    size_t size;                //Tamaño del arreglo
    size_t occupied_elements;   //Cantidad de elementos ocupados en la tabla
    heap *h;                    //Link al heap
}HTable_OA;

//IMPORTANTE: Nótese de la última estructura que se vincula el Heap directamente como parte de la hash table

/*Función para hacer una nueva tabla Hash con Open Addressing*/
HTable_OA* newHTableCap_OA(size_t index){
    //Reservamos memoria para la tabla Hash
    HTable_OA *HT = (HTable_OA*)malloc(sizeof(HTable_OA)*1);        //Reserva memoria para la tabla
    if(HT == NULL){                                                //Si HT es NULL, MALLOC no pudo reservar más memoria
        fprintf(stderr, "Cannot allocate memory for table.");
        exit(1);
    }
    //Reservamos memoria para el arreglo de los elementos hash (la tabla misma)
    HT->table = (hash_item*)calloc(HASH_SIZE[index], sizeof(hash_item));    
    if(HT->table == NULL){                                          //Si table es NULL, MALLOC no pudo reservar más memoria
        fprintf(stderr, "Cannot allocate memory for table.");
        exit(1);
    }
    //Si llegamos aquí, entonces sí se pudo reservar memoria
    HT->size = HASH_SIZE[index];                              //Indicar el tamaño de la tabla
    HT->index_size = index;                                   //Indicar el índice de tamaño
    //Inicializamos en 0 la cantidad de elementos ocupados en total(apenas es nueva la tabla)
    HT->occupied_elements = 0;
    for(size_t i = 0; i<HT->size; i++){
        HT->table[i].status = NOTVALID;
        HT->table[i].lazy_deleted = NO;
        HT->table[i].leapt = NO;
        HT->table[i].heap_index = 0;
    }

    //Se declara un nuevo Heap
    HT->h = newHeap();
    return HT;
    }


/*Aquí definimos una función para generar una tabla Hash con arreglos con el primer tamaño disponible*/
HTable_OA* newHTable_OA(){
    return newHTableCap_OA(0);
}

/*Función para liberar el espacio de toda la tabla (elemento por elemento)*/
void freeHTable_OA(HTable_OA *HT){
    //Se libera elemento por elemento
    for(size_t i=0; i<HT->size; i++){
        //Se libera los espacios reservados para el contenido en cada elemento
        //free(HT->table[i].rec.bytes);
    }
    free(HT->table);
    //Se libera espacio del Heap
    freeHeap(HT->h);
    assert(HT->table != NULL);//"Asegúrate de que el arreglo de cabezas no es nulo"
    free(HT);
}

//Funcion para sacar el módulo de una llave
static inline size_t hashFunction(uint32_t key, size_t hashSize){ //static inline hace que el compilador tome el argumento y opere hashFunction sin considerarla como funcion
    return key % hashSize;
}

/*Prototipos para poder usar la función de insertar en la función "Remodel"*/
hash_item* HTinsertRecord_OA(HTable_OA **HT, record *rec, int mode);
void heapifyUp(heap **h, size_t index, HTable_OA **HT);
void insertHeap(record *rec, heap **h, HTable_OA **HT);

/*Función para para expandir o reducir espacio: reserva memoria y reacomoda el contenido de un quash ya existente*/
HTable_OA* RemodelHTableCap_OA(HTable_OA *PreviousHT, int state, size_t mode){
    //Variable auxiliar para guardar el índice de tamaño de la tabla antigua
    size_t newIndex = PreviousHT->index_size;
    //Ahora aumentamos o disminuimos el tamaño de la tabla según el valor de "state"
    if(state==FULL)
        newIndex+=1;                                       //Incrementamos el valor del cap_type (avanzamos en el arreglo de capacidades)
    if(state==EMPTY)
        newIndex-=1;                                       //Decrementamos el valor del cap_type (retrocedemos en el arreglo de capacidades)

    //Aquí aseguramos que state no sea 0. Si es así, entonces hubo un erro al mandar llamar la función sin necesidad
    //...(DETENTE si la tabla no está ni llena ni vacía)
    assert(state!=0);
    //Creamos una nueva tabla con el nuevo índice
    HTable_OA *HT = newHTableCap_OA(newIndex);
    
    //Aquí se insertará cada elemento del Heap del quash anterior al nuevo
    size_t current_index = PreviousHT->h->index;
    for(size_t i=1; i<=current_index; i++){
        PreviousHT->h->index = 0;
        //Se inserta tanto en el Heap como en la tabla hash del quash
        hash_item *aux = HTinsertRecord_OA(&HT, &(PreviousHT->h->array[i].rec), mode);
        insertHeap(&(PreviousHT->h->array[i].rec), &(HT->h), &HT);
    }

    //Liberamos el espacio de la tabla antigua
    freeHTable_OA(PreviousHT);
    //Regresamos la nueva tabla (con el contenido incluído)
    return HT;
    }

/*Función para evaluar si la tabla está llena o vacía (relativamente hablando)*/
//NOTA: "operation" indica si se mandó llamar la función para insertar ("UP") o para borrar ("DOWN") elementos
int checkSizeOA(HTable_OA *HT, int operation){
    //Checamos si la cantidad de elementos ocupados es mayor al 50% de capacidad. Si es así, está llena.
    if((HT->occupied_elements>(HT->size/2))&&(operation==UP))
        return FULL;
    //Ahora, se evalúa si la cantidad de elementos ocupados es menor que un cuarto de la capacidad total
    //NOTA: Aquí le sumamos el cuadrado de la variable global "hist" (histéresis)
    size_t aux1 = HT->occupied_elements;
    size_t aux2 = HT->size/10 +(hist*hist);
    if((HT->occupied_elements<(aux2))&&(operation==DOWN)){
        //Por supuesto, si tenemos el menor tamaño posible, no mandamos "empty" para no reducir (ya no se puede)
        if((HT->index_size)>0)
            hist++;                 //Aumentamos el valor de la histéresis en 1 (cada vez que se reduzca la tabla)
            return EMPTY;
    }
    return 0;
}

/*Prototipo de función para checar los bytes entre dos contenidos y ver si son iguales o no*/
int checkMatchRecord(record *A, record *B);

//************************************FUNCIONES PARA LAS OPERACIONES BÁSICAS*******************************************************************************************
/************************SONDEO PARA BUSCAR ELEMENTOS***************************************/
/*NOTA: index es el resultado de la función hash original*/
/*Función para buscar un espacio de tabla disponible con double hashing*/
hash_item* DHFindKey(HTable_OA **HT, size_t key, record *rec){
    size_t index = hashFunction(key, (*HT)->size);
    //La variable i representa la cantidad de colisiones
    size_t i = 0;
    //Si hubo coincidencia con la llave, se regresa el hash item correspondiente
    if(checkMatchRecord(&((*HT)->table[index].rec), rec)==YES)
        return (&(*HT)->table[index]);
    //Ciclo que recorre toda la tabla hasta dar con un espacio disponible (función anticolisiones: f(i)= R - i mod R, ...
    //... siendo R un número primo menor a HASH_SIZE)
    //Se define primeramente R (véase comentario anterior) con el número primo previo al de HASH_SIZE según el
    //arreglo de capacidades posibles. Sólo se hace la excepción para cuando es la primera capacidad posible
    size_t R;
    //Si el tamaño de la tabla es el mínimo, establecemos R=3 (primo menor al mínimo tamaño, el cual es 5)
    if((*HT)->size <= 5){
        R = 3;
    }
    else
    //Se establece como R el primo menor anterior en el arreglo de capacidades
        R = HASH_SIZE[(*HT)->index_size - 1];
    size_t Hash2;
     while(((*HT)->table[index].lazy_deleted==YES || (*HT)->table[index].leapt==YES)){
        //Si hubo coincidencia con la llave, se regresa el hash item correspondiente
        if(checkMatchRecord(&((*HT)->table[index].rec), rec)==YES)
            return (&(*HT)->table[index]);
        //Se incremente la cantidad de colisiones en 1
        i++;
        //Se realiza aquí el double hashing
        Hash2 = R - hashFunction(key, R);
        index = hashFunction((index + i*Hash2),(*HT)->size);     //Aquí se aplica h2(i) = (x + f(i)) mod HASH_SIZE

    }
    //Si hubo coincidencia con la llave, se regresa el hash item correspondiente
        if(checkMatchRecord(&((*HT)->table[index].rec), rec)==YES)
            return (&(*HT)->table[index]);
    return NULL;
}

/***************************************************************************************/
/*Función para encontrar una llave en una tabla Hash*/
hash_item* HTfindkey_OA(HTable_OA **HT, uint32_t key, size_t mode, record *rec){
    switch (mode)
    {
    //Se manda llamar la función para buscar una llave según see el modo operado
    case DH:
        return DHFindKey(HT, key, rec);
        break;
    default:
        break;
    }
    return NULL;
}

/*Función para checar los bytes entre dos contenidos y ver si son iguales o no*/
int checkMatchRecord(record *A, record *B){
    //Si la longitud de A y B son diferentes, de antemano ya sabemos que no son iguales
    if(A->len != B->len)
	return NO;
    unsigned char *pA = (unsigned char*)A->bytes;            // Este "(unsigned char*)" es lo que conocemos como un untpype cast
    unsigned char *pB = (unsigned char*)B->bytes;
    //Si uno de los bytes es diferente entre sí de A y B, entonces no son iguales
    for(size_t i=0; i<B->len; i++){
        if(pA[i] != pB[i]){
	    return NO;}
    }
    //Si A y B pasaron las pruebas anteriores, entonces sí son iguales
    return YES;
}


/*Función para encontrar un record en una tabla Hash*/
hash_item* HTfindRecord_OA(HTable_OA **HT, record *rec, size_t mode){
    //Se calcula la llave de acuerdo al contenido
    uint32_t key = adler32((unsigned char*)rec->bytes, rec->len);               //Encuentro la llave asociada a record (una cadena de longitud "len")
    //Se manda llamar la función de encontrar llave
    hash_item *item = HTfindkey_OA(HT, key, mode, rec);
    if(item == NULL)
        return NULL;
    //Si se encontró la llave, se verifica si hay coincidencia en el contenido
    if(checkMatchRecord(rec, &(item->rec))==YES)
        return item;
    return NULL;
}

/************************TIPOS DE SONDEO PARA INSERTAR ELEMENTOS***************************************/
/*Función para buscar un espacio de tabla disponible con double hashing*/
size_t DoubleHashing(HTable_OA **HT, size_t key){
    size_t index = hashFunction(key, (*HT)->size);
    //La variable i representa la cantidad de colisiones
    size_t i = 0;
    //Ciclo que recorre toda la tabla hasta dar con un espacio disponible (función anticolisiones: f(i)= R - i mod R, ...
    //... siendo R un número primo menor a HASH_SIZE)
    //Se define primeramente R (véase comentario anterior) con el número primo previo al de HASH_SIZE según el
    //arreglo de capacidades posibles. Sólo se hace la excepción para cuando es la primera capacidad posible
    //size_t R = HASH_SIZE[(*HT)->index_size - 1];
    size_t R;
    if((*HT)->size <= 5){
        R = 3;
    }
    else
        R = HASH_SIZE[(*HT)->index_size - 1];
    size_t Hash2;
    while(((*HT)->table[index].status==VALID)){
        //Se incremente la cantidad de colisiones en 1
        i++;
        //Se marca el elemento actual como saltado
        (*HT)->table[index].leapt=YES;
        Hash2 = R - hashFunction(key, R);
        index = hashFunction((index + i*Hash2), (*HT)->size);     //Aquí se aplica h2(i) = (x + f(i)) mod HASH_SIZE
    }
    return index;
}

/*************************************************************************************************/

/*Función para insertar un elemento en una tabla hash*/
/*NOTA: la variable local "mode" es para indicar qué tipo de sonde se empleará*/
hash_item* HTinsertRecord_OA(HTable_OA **HT, record *rec, int mode){
    //Primeramente vamos a ver si la tabla tiene un tamaño grande. Si es así, la expandemos
    if(checkSizeOA(*HT, UP)==FULL){
        (*HT)=RemodelHTableCap_OA(*HT, FULL, mode);
    }
    //Se calcula la llave
    uint32_t key = adler32(rec->bytes, rec->len);
    //Usando la función para encontrar una llave, se evalúa si lo que regresa es nulo o no (si no lo es, quiere decir que ya estaba el contenido...
    //... en la tabla)
    hash_item* item = HTfindRecord_OA(HT, rec, mode);
    if(item != NULL){
    //Si ya estaba el elemento, se incrementa en uno el contador de multiplicidad en el heap
        (*HT)->h->array[item->heap_index].mult++;
        return item;
    }
    //Si la ejecución llega hasta aquí, el contenido no estaba presente.
    //Se realiza la función hash original
    size_t index = hashFunction(key, (*HT)->size);
    //A continuación realizamos la búsqueda de un espacio disponible según el tipo de sondeo elegido en MAIN
    switch (mode)
    {
    case DH:
        index = DoubleHashing(HT, key);
        break;
    default:
        break;
    }
    //Insertamos el record en el lugar encontrado
    (*HT)->table[index].key = key;
    (*HT)->table[index].status = VALID;
    (*HT)->table[index].rec.bytes = malloc(sizeof(rec->len));
    (*HT)->table[index].rec.len = rec->len;
    if((*HT)->table[index].rec.bytes == NULL){
        fprintf(stderr, "Cannot allocate memory for element!\n");
        return NULL;
    }
    //Se copia el contenido
    strcpy((*HT)->table[index].rec.bytes, rec->bytes);
    (*HT)->occupied_elements++;
    
    //Se guarda la ubicación (índice de tabla hash) en el elemento heap
    //OJO: Apenas se va insertar el elemento en el siguiente espacio del heap (por eso se le suma 1 al índice actual del heap)
    (*HT)->h->array[(*HT)->h->index+1].hash_index = index; 
    return (*HT)->table;
}

//Función para borrar un record en una tabla hash
hash_item* HTdeleteRecordOA(HTable_OA **HT, record *rec, size_t mode){
    //Se verifica si no exisitía antes el record en la tabla
    hash_item* item = HTfindRecord_OA(HT, rec, mode);
    //Si el item no estaba simplemente se regresa su valor
    if(item == NULL)
        return item;
    item ->status = NOTVALID;
    item ->lazy_deleted = YES;
    //Finalmente vamos a ver si la tabla tiene muchos elementos sin ocupar. Si es así, la reducimos
    if(checkSizeOA(*HT, DOWN)==EMPTY){
        if((*HT)->index_size>0){
            (*HT)=RemodelHTableCap_OA(*HT, EMPTY, mode);
        }
    }
    //Reducimos en uno el número de elementos ocupados
    if((*HT)->occupied_elements>0)
    	(*HT)->occupied_elements--;

    //Reducimos en uno su multiplicidad
    (*HT)->h->array[item->heap_index].mult--;
    return item;
}
/****************************************HEAPS******************************************************************/
//Comparador de records con valores negativos (rec compare)
int reccmp_n(record r1, record r2){
    char *c1 = (char*) r1.bytes;
    char *c2 = (char*) r2.bytes;
    //Caso donde ambos contenidos son negativos. Aquí se invierten las reglas del comparador de records 
    //... de num. positivos (si la primer cifra de A es mayor que la de B, entonces A es el menor)
    //NOTA: El signo negativo "-" es 45 en ASCII
    if(c1[0]==45 && c2[0]==45){
        size_t len = r1.len < r2.len ? r1.len : r2.len;  //Si _____, entonces (?) ____. Si no (:), haz_________
        for(size_t i = 0; i<len; i++){
            if(c1[i] > c2[i]){
                return -1;
            }
            if(c1[i] < c2[i]){
                return 1;
            }
        }
        if(r1.len > r2.len)
            return -1;
        if(r1.len > r2.len)
            return 1;
    }
    //Casos donde sólo un número es negativo
    if(c1[0]==45)
        return -1;
    if(c2[0]==45)
        return 1;
    return 0;
}

//Comparador de records (rec compare)
int reccmp(record r1, record r2){
    char *c1 = (char*) r1.bytes;
    char *c2 = (char*) r2.bytes;
    char a = c1[0];
    char b = 45;
    //La siguiente parte es para casos negativos
    if(c1[0]==45 || c2[0]==45){
        return reccmp_n(r1, r2);
    }
    //Casos donde el número de cifras es diferente
    if(r1.len > r2.len)
        return 1;
    if(r1.len < r2.len)
        return -1;
    //Caso donde el no. de cifras es el mismo
    size_t len = r1.len;
    //size_t len = r1.len < r2.len ? r1.len : r2.len;  //Si _____, entonces (?) ____. Si no (:), haz_________
    for(size_t i = 0; i<len; i++){
        if(c1[i] > c2[i]){
            return 1;
        }
        if(c1[i] < c2[i]){
            return -1;
        }
    }
    return 0;
}

/*Regresa el papá de un nodo (se divide la posición entre dos que es lo mismo que hacer un recorrimiento a la derecha)*/
static inline size_t parent(size_t pos){
    return pos >> 1;
}

/*Regresa el hijo izquierdo de un nodo. Se multiplica por 2 (es lo mismo que hacer un recorrimiento a la izquierda)*/
static inline size_t left_child(size_t pos){
    return pos << 1;
}


/*Regresala el hijo derecho de un nodo. Se multiplica por 2 (es lo mismo que hacer un recorrimiento a la izquierda) y se suma 1*/
static inline size_t right_child(size_t pos){
    return (pos << 1) + 1;
}

/*Función para heapify Up*/
void heapifyUp(heap **h, size_t index, HTable_OA **HT){
    //El siguiente ciclo While se rompe cuando llegamos a la raiz
    //Contador de HeapifyUps. Si es 0, entonces no se realizó ninguno y el elemento se insertó directamente en la última posición del Heap
    size_t contador = 0;
    while(1){
        size_t parent_index = parent(index);
        if(reccmp((*h)->array[index].rec, (*h)->array[parent_index].rec)==1){
            multiplicidad = (*h)->array[index].mult;
            if(contador == 0)
                 (*HT)->table[(*h)->array[index].hash_index].heap_index = (*HT)->h->index;
            break;
            }
        if(contador == 0)
            (*HT)->table[(*h)->array[index].hash_index].heap_index = (*HT)->h->index;
        heap_item aux = (*h)->array[index];      //Aquí empleamos una variable auxiliar
        
        //A continuación se hace el SWAP
        (*h)->array[index].rec = (*h)->array[parent_index].rec;
        (*h)->array[index].mult = (*h)->array[parent_index].mult;
        //Aquí también se hace SWAP ente las ubicaciones (índices) del elemento en el heap presentes en los respectivos elementos hash
        (*HT)->table[(*h)->array[index].hash_index].heap_index = parent_index;
        (*h)->array[index].hash_index = (*h)->array[parent_index].hash_index;
        
        (*h)->array[parent_index].rec = aux.rec;
        (*h)->array[parent_index].mult = aux.mult;
        //Aquí también se hace SWIMP ente las ubicaciones (índices) del elemento en el heap presentes en los respectivos elementos hash
        (*HT)->table[(*h)->array[parent_index].hash_index].heap_index = index;
        (*h)->array[parent_index].hash_index = aux.hash_index;
        
        index = parent_index;
        contador++;
    }
    return;
}

void heapifyDown(heap **H, size_t index, HTable_OA **HT){
    heap *h = *H;
    //El siguiente While se rompe cuando llegamos a la generación donde se cumple la condición Heap dado un elemento inicial (siempre que)
    //el nodo de interés sea menor a los hijos
    while(1){
        size_t left = left_child(index);
        size_t right = right_child(index);
        if((reccmp(h->array[index].rec, h->array[left].rec)==-1) && (reccmp(h->array[index].rec, h->array[right].rec)==-1)){
            size_t prueba = h->array[index].mult;
            break;
        }
        //A continuación se hace el SWAP con el hijo menor
        size_t min_index;
        if(reccmp(h->array[left].rec, h->array[right].rec)==-1){
            min_index = left;
        }
        else{
            min_index = right;
        }
        heap_item aux = h->array[index];
        h->array[index].rec = h->array[min_index].rec;
        h->array[index].mult = h->array[min_index].mult;
        //Aquí también se hace SWAP ente las ubicaciones (índices) del elemento en el heap presentes en los respectivos elementos hash
        (*HT)->table[h->array[index].hash_index].heap_index = min_index;
        h->array[index].hash_index = h->array[min_index].hash_index;

        h->array[min_index].rec = aux.rec;
        h->array[min_index].mult = aux.mult;
        //Aquí también se hace SWAP ente las ubicaciones (índices) del elemento en el heap presentes en los respectivos elementos hash
        (*HT)->table[h->array[min_index].hash_index].heap_index = index;
        h->array[min_index].hash_index = aux.hash_index;
        
        index = min_index;
    }
    return;
}

/*Generador de una estructura Heap*/
heap* newHeapCap(size_t cap){
    heap *new_heap = (heap*)malloc(sizeof(heap)*1); 
    if(new_heap == NULL){
        fprintf(stderr, "Error en malloc!\n");
        exit(1);
    }
    new_heap->array = (heap_item*)malloc(sizeof(heap_item)*cap);
    if(new_heap->array == NULL){
        fprintf(stderr, "Error en malloc!\n");
        exit(1);
    }
    new_heap->cap = cap;
    new_heap->index = 0;            //Colocamos el índice en 0 (porque vamos a empezar a ingresar elementos desde el 1)
    new_heap->array[0].rec.bytes = INF_N;     //Inicializamos la raíz con infinito negativo (el número menor posible)
    new_heap->array[0].rec.len = strlen(INF_N);
    for(size_t i=1; i<cap; i++){
        new_heap->array[i].rec.bytes = (void*)malloc(sizeof(INF_P));
        new_heap->array[i].rec.bytes = INF_P; //Inicializamos los hijos con infinito positivo (el número mayor posible)
        new_heap->array[i].rec.len = strlen(INF_P);
        new_heap->array[i].mult = 1;           //Iniciamos la multiplicidad de cada caso en 1
    }
    return new_heap;
}

/*Función para generar un Heap de 1024 elementos*/
heap* newHeap(){
    return newHeapCap(1024);
}

/*Función para liberar espacio de memoria ocupada por un heap*/
void freeHeap(heap *h){
    free(h->array);
    free(h);
}

/*Prototipo para insert*/
void insertHeap(record *rec, heap **h, HTable_OA **HT);

/*Función para expandir un heap (crear un heap con una mayor extención para ahí colocar un )*/
heap* RemodelHeap(heap *previousHeap, HTable_OA **HT){
    //Se incrementa el espacio por el doble del tamaño anterior
    heap *h = newHeapCap(previousHeap->cap*2);
    size_t index = previousHeap->index;
    //Se inserta el contenido del heap anterior al nuevo
    for(size_t i=1; i<=index; i++){
        insertHeap(&previousHeap->array[i].rec, &h, HT);
    }
    //Se libera el heap anterior
    freeHeap(previousHeap);
    return h;
}

/*Función para insertar un nodo en el Heap. Recuerda que "**" es la dirección de la dirección*/
void insertHeap(record *rec, heap **h, HTable_OA **HT){
    //Aquí se evaluará si aún hay espacio para introducir un nuevo elemento. Si no, hay que incrementarlo
    if((*h)->index == (*h)->cap-1){
        *h = RemodelHeap((*HT)->h, HT);
    }
    heap *H = *h; 
    //Se inserta el elemento según el orden de un Heap
    H->array[H->index+1].rec.bytes = (void*)malloc(sizeof(rec->len));
    memcpy(H->array[H->index+1].rec.bytes, rec->bytes, strlen(rec->bytes));
    size_t longitud = rec->len;
    H->array[H->index+1].rec.len = longitud;
    
    (*HT)->h->index = (*HT)->h->index+1;
    heapifyUp(&H, H->index, HT);             //Se realiza el proceso de Heapify Up
}

/*Función para borrar el elemento más chico del Heap (la raíz)*/
void deleteMin(HTable_OA **HT){
    heap *H = (*HT)->h;
    //Si el índice del Heap está en 0, quiere decir que no hay un elemento mínimo presente
    if(H->index==0){
        printf("elemento minimo no presente (tabla esta vacia)");
        return;
    }
    //Se verifica ahora si el elemento mínimo del heap tiene multiplicidad > 1
    if(H->array[1].mult>1){
        //Si es el caso que la multiplicidad es mayor a 0, sólo se decrementa en 1
        H->array[1].mult--;
        //Impresión en pantalla
        char *str = (char*)H->array[1].rec.bytes;
        printf("elemento minimo ");
        for(size_t j=0; j<H->array[1].rec.len; j++){
                printf("%c", str[j]);
            }
        printf(" se decremento, nuevo contador = %ld\n", H->array[1].mult);
        return;
    }
    //Borramos en la hash table
    HTdeleteRecordOA(HT, &H->array[1].rec, DH);
    //Impresión en pantalla
    char *str = (char*)H->array[1].rec.bytes;
    printf("elemento minimo ");
    for(size_t j=0; j<H->array[1].rec.len; j++){
            printf("%c", str[j]);
        }
    printf(" eliminado\n");
    //Swap del último elemento insertado con la raíz del heap. "Borramos" a la raiz (asignamos el valor INF_P)
    size_t ubication = 1;
    size_t LastIndex = H->index;
    size_t pruebaUb = (*HT)->table[H->array[ubication].hash_index].heap_index;
    size_t pruebaI = (*HT)->table[H->array[LastIndex].hash_index].heap_index;
    heap_item aux = H->array[ubication];

    H->array[ubication].rec = H->array[LastIndex].rec;
    H->array[ubication].mult = H->array[LastIndex].mult;
    (*HT)->table[H->array[ubication].hash_index].heap_index = LastIndex;
    H->array[ubication].hash_index = H->array[LastIndex].hash_index;
    
    H->array[LastIndex].rec.bytes = INF_P;
    H->array[LastIndex].mult = aux.mult;
    (*HT)->table[H->array[LastIndex].hash_index].heap_index = ubication;
    H->array[LastIndex].hash_index = aux.hash_index;
    
    pruebaUb = (*HT)->table[H->array[ubication].hash_index].heap_index;
    pruebaI = (*HT)->table[H->array[LastIndex].hash_index].heap_index;

    //Hacemos heapifyDown
    heapifyDown(&H, 1, HT);
    H->index--;
    return;
}

/*Función para borrar un elemento del Heap conociendo el índice correspondiente (variable "ubication")*/
void deleteHeap(heap **h, size_t ubication, HTable_OA **HT){
    heap *H = *h;
    //Swap del último elemento insertado con la raíz. "Borramos" a al elemento de interés (asignamos el valor INF_P)
    size_t LastIndex = H->index;
    size_t pruebaUb = (*HT)->table[H->array[ubication].hash_index].heap_index;
    size_t pruebaI = (*HT)->table[H->array[LastIndex].hash_index].heap_index;
    heap_item aux = H->array[ubication];
    H->array[ubication].rec = H->array[LastIndex].rec;
    H->array[ubication].mult = H->array[LastIndex].mult;
    (*HT)->table[H->array[ubication].hash_index].heap_index = LastIndex;
    H->array[ubication].hash_index = H->array[LastIndex].hash_index;
    
    H->array[LastIndex].rec.bytes = INF_P;
    H->array[LastIndex].mult = aux.mult;
    (*HT)->table[H->array[LastIndex].hash_index].heap_index = ubication;
    H->array[LastIndex].hash_index = aux.hash_index;
    
    pruebaUb = (*HT)->table[H->array[ubication].hash_index].heap_index;
    pruebaI = (*HT)->table[H->array[LastIndex].hash_index].heap_index;

    //Hacemos heapifyDown
    heapifyDown(h, ubication, HT);
    //Decrementamos el índice (procurando que nunca sea menor a 0)
    if(H->index > 0)
        H->index--;
    return;
}

void printElement(heap_item *item){
    char *str = (char*)item->rec.bytes;
    str = item->rec.bytes;
    for(size_t j=0; j<item->rec.len; j++){
        printf("%c", str[j]);
    }
    printf(" ");
}

/*Función para imprimir un heap*/
void print_Heap(HTable_OA *H){
    heap *h = H->h;
    for(size_t i=1; i<=h->index; i++){
        char *str = (char*)h->array[i].rec.bytes;
        for(size_t j=0; j<h->array[i].rec.len; j++){
            printf("%c", str[j]);
        }
        printf(" ");
    }
    printf("\n");
    }

/**************************Funciones para quash*********************************************/
/*Función para verificar si está insertado un elemento (e indicar su multiplicidad)*/
void LookUpElement(HTable_OA **HT, record *rec){
    hash_item *aux = HTfindRecord_OA(HT, rec, DH);
    //Se verifica que el hash item correspondiente exista y esté marcado como válido (no borrado)
    if(aux != NULL && aux->status==VALID){
        size_t contador = (*HT)->h->array[aux->heap_index].mult;
        printf("elemento encontrado, contador = %ld\n", contador);
    }
    else{
        printf("elemento no encontrado\n");
    }
}

/*Función para insertar un elemento*/
void InsertElement(HTable_OA **HT, record *rec){
    //Prueba
    unsigned char *A = (unsigned char*)(*HT)->h->array[(*HT)->h->index].rec.bytes;
    unsigned char *pA = (unsigned char*)rec->bytes;
    char *str = (char*)(*HT)->h->array[5].rec.bytes;
    //Se verifica si ya estaba el record
    hash_item *aux = HTfindRecord_OA(HT, rec, DH);
    //Si ya estaba, sólo se aumenta en uno el valor de su multiplicidad y se marca como VÁLIDO en la tabla hash
    if(aux!=NULL){
        size_t prueba = (*HT)->h->array[aux->heap_index].mult;
        (*HT)->h->array[aux->heap_index].mult++;
        multiplicidad = (*HT)->h->array[aux->heap_index].mult;
        aux->status = VALID;
        
    }
    //Si no estaba, se procede a insertar
    else{
        //Se inserta en la tabla hash y se guarda su ubicación (en el heap) en el hash item correspondiente
        aux = HTinsertRecord_OA(HT, rec, DH);
        //Se guarda la ubicación del elemento en el heap (índice) en el respectivo hash_item
        //OJO: Como apenas se va a insertar en el heap, se suma 1 al índice
        aux->heap_index = (*HT)->h->index+1;
        //Se inserta en el Heap
        A = (unsigned char*)(*HT)->h->array[(*HT)->h->index].rec.bytes;
        (*HT)->h->array[aux->heap_index].mult = 1;
        insertHeap(rec, &(*HT)->h, HT);
        A = (unsigned char*)(*HT)->h->array[(*HT)->h->index].rec.bytes;
    }
    printf("elemento insertado, contador = %ld\n", multiplicidad);
}

/*Función para borrar un elemento*/
void DeleteElement(HTable_OA **HT, record *rec){
    //Se verifica primero si el contador de multiplicidad tiene valor mayor a 1
    hash_item *aux = HTfindRecord_OA(HT, rec, DH);
    if(aux==NULL || aux->status==NOTVALID){
        printf("elemento no presente en la tabla\n");
        return;
    }
    size_t prueba = aux->heap_index;
    if((*HT)->h->array[aux->heap_index].mult>1){
        //Si es el caso que la multiplicidad es mayor a 0, sólo se decrementa en 1
        (*HT)->h->array[aux->heap_index].mult--;
        //Impresión en pantalla
        char *str = (char*)(*HT)->h->array[aux->heap_index].rec.bytes;
        printf("elemento ");
        for(size_t j=0; j<(*HT)->h->array[aux->heap_index].rec.len; j++){
                printf("%c", str[j]);
            }
        printf(" se decremento, nuevo contador = %ld\n", (*HT)->h->array[aux->heap_index].mult);
        return;
    }
    //Si la ejecución llega hasta aquí, entonces la multiplicidad es 1
    //Se borra en la tabla y se guarda la ubicación en el heap
    aux = HTdeleteRecordOA(HT, rec, DH);
    if(aux != NULL){
        size_t ubication = aux->heap_index;
        //Se borra en el heap
        deleteHeap(&(*HT)->h, ubication, HT);
        size_t contador = (*HT)->h->array[aux->heap_index].mult;
        printf("elemento eliminado\n");
    }
    else{
        printf("elemento no presente en la tabla\n");
        return;
    }
}

/**************************MAIN******************************/
int main(){
    HTable_OA *quash = newHTable_OA();
    size_t mode = DH;
    record rec;
    char buffer[100];
    
    while(fgets(buffer, 100, stdin) != NULL){
        unsigned char *A = (unsigned char*)quash->h->array[quash->h->index].rec.bytes;
        char command[20] = " ";
        char number[30] = " ";
        sscanf(buffer, "%s %s", command, number);
        rec.bytes = number;
        rec.len = strlen(number);
        if(strcmp("insert", command)==0){               //insertar
            InsertElement(&quash, &rec);
            continue;
        }
        if(strcmp("delete", command)==0){               //borrar
            sscanf(buffer, "%s %s", command, number);
            memcpy(rec.bytes, number, strlen(number));
            rec.len = strlen(number);
            DeleteElement(&quash, &rec);
            continue;
        }
       
        if(strcmp("lookup", command)==0){                //Encontrar un elemento
            sscanf(buffer, "%s %s", command, number);
            memcpy(rec.bytes, number, strlen(number));
            rec.len = strlen(number);
            LookUpElement(&quash, &rec);
            continue;
        }
        if(strcmp("deleteMin", command)==0){                //Borrar el elemento menor 
            deleteMin(&quash);
            continue;
        }
         if(strcmp("print", command)==0){                //imprimir
            print_Heap(quash);
            continue;
        }
        if(strcmp("stop", command)==0){                 //Parar (crea un ciclo infinito para medir memoria en servidor)
            size_t i = 0;
            while(i<1){
                continue;
            }
            continue;
        }
        if(strcmp("exit", command)==0)                  //salir
            break;
    }
    freeHTable_OA(quash);
    printf("¡Gracias!\n"); 
    return 0;
}