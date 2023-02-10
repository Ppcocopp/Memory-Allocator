/* On inclut l'interface publique */
#include "mem.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Définition de l'alignement recherché
 * Avec gcc, on peut utiliser __BIGGEST_ALIGNMENT__
 * sinon, on utilise 16 qui conviendra aux plateformes qu'on cible
 */
#ifdef __BIGGEST_ALIGNMENT__
#define ALIGNMENT __BIGGEST_ALIGNMENT__
#else
#define ALIGNMENT 16
#endif

/* structure placée au début de la zone de l'allocateur

   Elle contient toutes les variables globales nécessaires au
   fonctionnement de l'allocateur

   Elle peut bien évidemment être complétée
*/
struct allocator_header {
        size_t memory_size;
	mem_fit_function_t *fit;
};

/* La seule variable globale autorisée
 * On trouve à cette adresse le début de la zone à gérer
 * (et une structure 'struct allocator_header)
 */
static void* memory_addr;

static inline void *get_system_memory_addr() {
	return memory_addr;
}

static inline struct allocator_header *get_header() {
	struct allocator_header *h;
	h = get_system_memory_addr();
	return h;
}

static inline size_t get_system_memory_size() {
	return get_header()->memory_size;
}


struct fb {
	size_t size;
	struct fb* next;
};

//Renvoie l'adresse de fin de notre zone de stockage
void* fin(){
	return memory_addr+get_system_memory_size();
}

//Renvoie l'adresse de la zone suivante (libre ou non) en memoire
void* prochain(void* list){
	return (((struct fb*)list)->size)+list+sizeof(struct fb);
}

//Renvoie l'adresse de l'en-tete de la liste chainee de zones libres
void* getFbh(){
	void* p = get_header();
	p+=sizeof(struct allocator_header);
	return p;
}

//renvoie 1 si la zone pointee est libre, renvoie 0 sinon
int estLibre(void* list){
	if(((struct fb*)list)->next!=NULL) return 1;	//a un prochain dans la liste chainee donc libre
	void* fb_parcours = getFbh();
	while(fb_parcours!=NULL && fb_parcours<=fin()){
		if(fb_parcours == list) return 1;
		if(fb_parcours>list) return 0;		//on a depasse notre fb dans la liste chainee
		fb_parcours = ((struct fb*)fb_parcours)->next;
	}
	return 0;
}

//Initialise notre allocateur de memoire
void mem_init(void* mem, size_t taille)
{
        memory_addr = mem;
        *(size_t*)memory_addr = taille;
	/* On vérifie qu'on a bien enregistré les infos et qu'on
	 * sera capable de les récupérer par la suite
	 */
	assert(mem == get_system_memory_addr());
	assert(taille == get_system_memory_size());
	
	void* fbh = getFbh();
	((struct fb*)fbh)->size=0;
	((struct fb*)fbh)->next = fbh+sizeof(struct fb);
	fbh = ((struct fb*)fbh)->next;		//notre premiere cellule de zone libre
	((struct fb*)fbh)->size=get_system_memory_size()-sizeof(struct allocator_header)-2*sizeof(struct fb);	//Notre premiere zone libre contient toute la memoire restante
	((struct fb*)fbh)->next = NULL;

	mem_fit(&mem_fit_first);
}

//Affiche chaque zone avec leur adresse, leur taille et leur disponibilite
void mem_show(void (*print)(void *, size_t, int)) {
	void* list = getFbh();
	list += sizeof(struct fb);
	while (list!=NULL && list<fin()) {
		if(((struct fb*)list)->size>0) print(list, ((struct fb*)list)->size, estLibre(list));
		list = prochain(list);
	}
}

void mem_fit(mem_fit_function_t *f) {
	get_header()->fit = f;
}

void *mem_alloc(size_t taille) {
	__attribute__((unused)) /* juste pour que gcc compile ce squelette avec -Werror */
	struct fb *trouve=get_header()->fit((struct fb*) getFbh(),taille);
	if(trouve != NULL){

		//recherche de la derniere zone libre avant notre zone trouvee
		void* precedent = getFbh();
		while(((struct fb *)precedent)->next != trouve) precedent = ((struct fb *)precedent)->next;

		//recherche si petit morceau de memoire restant
		if(prochain(trouve)-((void*)trouve+sizeof(struct fb)+taille)<=sizeof(struct fb)){
			taille=trouve->size;
			printf("Finalement la taille sera : %ld\n", taille);
			((struct fb *)precedent)->next = trouve->next;
			trouve->next = NULL;
		}else{
			//Creation de notre nouvelle zone en separant la zone libre en 2
			void* nouveau = (void*)trouve+taille+sizeof(struct fb);
			((struct fb *)precedent)->next = nouveau;
			((struct fb *)nouveau)->next = trouve->next;
			trouve->next = NULL;
			((struct fb *)nouveau)->size = trouve->size-sizeof(struct fb)-taille;
			trouve->size = taille;
		}
	}
	return trouve;
}

//Liberation d'une zone allouee passee en parametre
void mem_free(void* zone){
	void* precedent_libre = getFbh();

	//recherche si l'adresse est bien celle d'une zone
	int valid = 0;
	precedent_libre=prochain(precedent_libre);
	while(precedent_libre!=fin()){
		if(precedent_libre==zone)valid = 1;
		precedent_libre=prochain(precedent_libre);
	}
	if(((struct fb*)zone)==NULL || valid==0){
		printf("Ce n'est pas une adresse valide\n");
		return;
	}
	if(estLibre(zone)){
		printf("Cette zone est déjà libre\n");
		return;
	}

	//recherche de la derniere zone libre avant notre zone
	precedent_libre = getFbh();
	while((void*)(((struct fb*)precedent_libre)->next) < zone){
		precedent_libre=((struct fb*)precedent_libre)->next;
	}

	//liberation de la zone (sans fusion)
	((struct fb*)zone)->next = ((struct fb*)precedent_libre)->next;
	((struct fb*)precedent_libre)->next = zone;

	//fusion avant :
	if(precedent_libre!=getFbh() && prochain(precedent_libre)==zone){
		((struct fb*)precedent_libre)->size = ((struct fb*)precedent_libre)->size+((struct fb*)zone)->size+sizeof(struct fb);
		((struct fb*)precedent_libre)->next = ((struct fb*)zone)->next;
		zone=precedent_libre;
	}

	//recherche de la derniere zone libre avant notre zone (au cas où il ait fusione avant)
	precedent_libre = getFbh();
	while((void*)(((struct fb*)precedent_libre)->next) < zone){
		precedent_libre=((struct fb*)precedent_libre)->next;
	}

	//fusion apres :
	if(prochain(zone)!=fin() && ((struct fb*)zone)->next!=NULL && ((struct fb*)zone)->next==prochain(zone)){
		((struct fb*)zone)->size = ((struct fb*)zone)->size+sizeof(struct fb)+((struct fb*)prochain(zone))->size;
		((struct fb*)zone)->next = ((struct fb*)prochain(zone))->next;
	}
}

//Renvoie l'adresse de la premiere zone assez grande
struct fb* mem_fit_first(struct fb *list, size_t size){
	list = list->next;
	while(list!=NULL){
		if(list->size>=size) return list;
		list = list->next;
	}
	return NULL;
}

/* Fonction à faire dans un second temps
 * - utilisée par realloc() dans malloc_stub.c
 * - nécessaire pour remplacer l'allocateur de la libc
 * - donc nécessaire pour 'make test_ls'
 * Lire malloc_stub.c pour comprendre son utilisation
 * (ou en discuter avec l'enseignant)
 */
size_t mem_get_size(void *zone) {
	/* zone est une adresse qui a été retournée par mem_alloc() */

	/* la valeur retournée doit être la taille maximale que
	 * l'utilisateur peut utiliser dans cette zone */
	struct fb* z = (struct fb *) zone;
	return z->size;
}

/* Fonctions facultatives
 * autres stratégies d'allocation
 */
struct fb* mem_fit_best(struct fb* list, size_t size){
	int diff;
	struct fb* l;
	if(list->size>size){
		diff=(size-list->size);
		l=list;
	}else{
		while(list->next != NULL){
			if(list->size-size < diff){
				diff=(size-list->size);
				l=list;
				list=list->next;
			}else{
				list=list->next;
			}
		}
	}
	return l;
}

struct fb* mem_fit_worst(struct fb *list, size_t size){
	int diff;
	struct fb* l;
	if(list->size>size){
		diff=(size-list->size);
		l=list;
	}else{
		while(list->next != NULL){
			if(list->size-size > diff){
				diff=(size-list->size);
				l=list;
				list=list->next;
			}else{
				list=list->next;
			}
		}
	}
	return l;
}
