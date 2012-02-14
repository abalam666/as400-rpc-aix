/*
* Serveur de socket pour OS400
* Lancer le serveur avec la commande : ./act400server 4443 192.168.3.30 2<&1
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @date 2012-02-03
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <resolv.h>
#include <arpa/inet.h>

//#define fetch_size_of(x) (sizeof (x) / sizeof (x)[0])

// Variables globales
volatile sig_atomic_t suicide = 0; /* controls program termination */

// Multithread
int childpid=-1;
int nb_threads=0;
int max_threads=3;
int timeout=3;
struct sigaction sa;
int newsockfd;

// Function prototypes
void sigchld_handler(int s); /* décrémenter les signaux */
int execOS400(char *cmd, char *result, int result_size, char *transform); /* exécution sur OS400 */
int checkIP(char *source, char *search); /* l'IP est-elle acceptée */
void trim(char * s); /* enlever les espaces avant et après */
void error(const char *msg); /* gestion d'erreur standard */
int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1); /* calcul de différence de temps */
void termination_handler (int signum); /* clean up before termination */

/* Fonction qui veille aux signaux de fins de threads, permet de décrémenter le compteur
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param int s Signal
*/
void sigchld_handler(int s) {
	int status=0;
	if (childpid>0) {
    	while(waitpid(-1, NULL, WNOHANG) > 0);
		nb_threads--;
		printf("--SERVEUR-- Nouveau thread libre (%d en travail sur %d max)...\n",nb_threads,max_threads);
	} else {
    	while(waitpid(-1, &status, WNOHANG) > 0);		
		printf("Enfant [%d] signal recu : %d...\n",getpid(),status);
	}
}

/* Execute une commande OS400
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *cmd Syntaxe à exécuter
* @param char *result Résultat de la commande
* @param int result_size Taille du résultat
* @param char *transform Transformation de la syntaxe, par exemple "SYSTEM \"ACTCTL/%s\""
* @return int 0 si la commande s'est bien exécutée, -1 si le code d'erreur n'est pas trouvé en prefixe du retour standard STDIN
*/
int execOS400(char *cmd, char *result, int result_size, char *transform) {
	char buf[2048]; // variable temporaire
    FILE* fp; // Descripteur de lecture de l'exécution de la commande
//	bzero(buf,sizeof(buf)); // Clean du buffer
	bzero(result,result_size); // Clean du result
	
	snprintf(buf, sizeof buf, transform, cmd);
	printf("Transformation de la commande en : %s\n",buf);
	
	// Execution de la commande
	fp = popen(buf,"r");
	fread(result,1,result_size,fp);
	fclose(fp);
	printf("La commande a retourne :\n%s",result);
	
	bzero(buf,sizeof(buf));
	strncpy(buf, (char *)result, 8);
	if (strcmp(buf,"CPD0030:")==0) {
		return -1;
	} else {
		return 0;
	}
}

/* Prédicat qui analyse si la chaine recherchée est trouvée dans la chaine source séparée par virgule
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *source Chaine à analyser, composée de sous chaines séparées par virgules
* @param char *search Chaine recherchée
* @return int 0 si trouvé, -1 sinon
*/
int checkIP(char *source, char *search) {
	char tmp[strlen(source)+1]; // variable temporaire
	bzero(tmp,sizeof(source)); // Clean du result
	strcpy(tmp,source);
	char *p;
	p = strtok (tmp, ",");
	while (p != NULL){
//printf ("Chaine trouvee : %s\n", p);
		if (strcmp(p,search)==0) {
			return 0;
		}
		p = strtok (NULL, ",");
	}
	return -1;
}

/* Supprime les espaces qui entourent une chaine
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *s Chaine à modifier
*/
void trim(char * s) {
    char * p = s;
    int l = strlen(p);
    while(isspace(p[l - 1])) p[--l] = 0;
    while(* p && isspace(* p)) ++p, --l;
    memmove(s, p, l + 1);
}

/*
* Procédure d'erreur standard
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *msg Message d'erreur
*/
void error(const char *msg) {
	perror(msg);
	exit(1);
}

/* Return 1 if the difference is negative, otherwise 0.  */
int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1) {
    long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
    result->tv_sec = diff / 1000000;
    result->tv_usec = diff % 1000000;
    return (diff<0);
}

/* Modifie le flag suicide à 1 */
void termination_handler (int signum){
	suicide=1;
	printf("--termination_handler %d je close le newsockfd (%d) ...\n",getpid(),newsockfd);
	signal(signum, termination_handler);
}

void handletimeout() {
	printf("Thread enfant handletimeout %d newsockfd=%d",getpid(),newsockfd);
	close(newsockfd);
	exit(0);
}

/*
* Programme de serveur de socket
* @author Yann GAUTHERON <ygautheron@absystech.fr>
*/
int main(int argc, char *argv[]) {
	const char* transformPrimary = "system \"ACTCTL/%s\"";
	const char* transformSecondary = "system \"ACTCTLSPE/%s\"";
	const char* transformForced = "system \"%s\"";
	
	// Watchdog PID
	struct timeval tvBegin, tvEnd, tvDiff;
	int watchdogpid;
	int childpidToKill;

	signal(SIGCHLD, SIG_IGN);
	
	int sockfd, portno,cli_portno;
	socklen_t clilen;
	char buffer[2048];
	struct sockaddr_in serv_addr, cli_addr;
	char ipstr[INET6_ADDRSTRLEN];
	int n;
    char result[42000]; // Buffer de résultat de la commande
	if (argc < 2) {
		fprintf(stderr,"ERREUR, aucun port fourni en parametre. Usage : %s port [timeout] [max_threads (3 par defaut)] [ip_autorisee1[,ip_autorisee2[...]]]\n",argv[0]);
		exit(1);
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		error("ERREUR ouverture de socket");
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	if (argc >= 3) {
		max_threads = atoi(argv[2]);
	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	printf("--SERVEUR-- Socket cree sur le port %i.\n",portno);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		error("ERREUR bind socket");
	}
	listen(sockfd,5);
	clilen = sizeof(cli_addr);
	
	// Signaux des enfants
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
	
	while (1) {
		printf("--SERVEUR-- Socket en attente d'acceptation...\n");
		
		// Calcul du nombre de threads disponibles
		if (nb_threads>=max_threads) {
			printf("--SERVEUR-- Maximum de thread atteint, attente d'un slot libre (%d en travail sur %d max)...\n",nb_threads,max_threads);
			//wait();
		} else {
		
			newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
			
			switch(childpid=fork()){
				case -1://error
					fprintf(stderr, "Error spawning the child %d\n",errno);
					exit(0);
					
				case 0://thread enfant
					childpidToKill=getpid();
					signal(SIGALRM, handletimeout());
					alarm(timeout);

					switch(watchdogpid=fork()){
						case -1://error enfant
							fprintf(stderr, "Error spawning the child %d\n",errno);
							exit(0);
							
						case 0://thread petit-enfant qui surveille enfant
							printf("--WATCHDOG [%d]-- Check timeout limite a %d secondes\n",childpidToKill,timeout);
							gettimeofday(&tvBegin, NULL);
							while(1){
								gettimeofday(&tvEnd, NULL);
								timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
								if (tvDiff.tv_sec+tvDiff.tv_usec/1000000 > timeout) {
									printf("--WATCHDOG [%d]-- %ld.%06ld (limite a %d secondes)\n", childpidToKill, tvDiff.tv_sec, tvDiff.tv_usec, timeout);
									//suicide=1;
									close(newsockfd);
									kill(childpidToKill,9);
									break;
								}
							}
							exit(0);
							
						default://thread enfant
							if (signal (SIGTERM, termination_handler) == SIG_IGN){
								signal (SIGTERM, SIG_IGN);
							}
							signal (SIGINT, SIG_IGN);
							signal (SIGHUP, SIG_IGN);						
						
							printf("Connexion ouverte enfant %d.\n",getpid());
							if (newsockfd < 0) {
								error("ERREUR accept socket");
							}
							bzero(buffer,sizeof(buffer)); // Clean du buffer
							
							// Réception du client
							getpeername(newsockfd, (struct sockaddr*)&cli_addr, &clilen);	
							struct sockaddr_in *s = (struct sockaddr_in *)&cli_addr;
							cli_portno = ntohs(s->sin_port);
							inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
							
							printf("Socket client connecte, en attente de sa commande %s:%i.\n",ipstr,cli_portno);
							
							// Vérification de l'IP acceptée
							if (argc<3 || checkIP(argv[3],ipstr)==0) {
								// Lecture du message
								n = read(newsockfd,buffer,sizeof(buffer)-1);
								if (n < 0) {
									error("ERREUR lecture depuis socket");
								}
								
								//printf("Avant trim :|%s|\n",buffer);
								trim(buffer);
								//printf("Apres trim :|%s|\n",buffer);
								//printf("Reception de la commande : %s\n",buffer);

								if (strstr(buffer,"ACTCTLSPE/")!=NULL) {
									// Prefixe forcé
									printf("Prefixe forced\n");
									execOS400(buffer, (char *)&result, sizeof(result), (char *)transformForced);
								} else {
									// Si la commande échoue, on change le prefixe en ACTCTLSPE/
									if (execOS400(buffer, (char *)&result, sizeof(result), (char *)transformPrimary)!=0) {
										//printf("La commande ne semble pas exister dans la bibliotheque primaire (%s), essai dans la bibliotheque secondaire...\n","CPD0030:",result);
										execOS400(buffer, (char *)&result, sizeof(result), (char *)transformSecondary);
									}
								}
								
								// Envoi au client
								printf("Envoi du resultat au client.\n");
								n = write(newsockfd,result,sizeof(result));
								if (n < 0) {
									error("ERREUR ecriture vers socket");
								}	
							} else {
								fprintf(stderr,"ERREUR, adresse IP non autorisee (differente de %s).\n",argv[3]);
							}
			//printf("Attente 15 secondes enfant %d.\n",getpid());
			//sleep(15);
							printf("Connexion fermee enfant %d.\n",getpid());
							close(newsockfd);
							kill(watchdogpid,9);
							exit(0);
					}
						
				default://serveur pere
					nb_threads++;
					printf("--SERVEUR-- Changement nombre de thread en cours (%d en travail sur %d max)...\n",nb_threads,max_threads);
					printf("--SERVEUR-- Connexion fermee.\n");
					close(newsockfd);
					//free(newsockfd);
			}		
		
		}
		// Terminer le fils
		//printf("--SERVEUR-- Attente 5 secondes\n");
		//sleep(5);

	}
	close(sockfd);
	return 0; 
}