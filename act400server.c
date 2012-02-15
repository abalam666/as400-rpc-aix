/*
* Serveur de socket pour OS400
* Lancer le serveur avec la commande : ./act400server port [max_threads] [ip_autorisee1[,ip_autorisee2[...]]] [timeout (en secondes)] 2<&1
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

// Constantes
const char *version = "1.0.3";
const char *transformPrimary = "system \"ACTCTL/%s\" 2<&1";
const char *transformSecondary = "system \"ACTCTLSPE/%s\" 2<&1";
const char *transformForced = "system \"%s\" 2<&1";

// Multithread
int childpid=-1;
int nb_threads=0;
int max_threads=10;
int timeout=0;
struct sigaction sa;
int newsockfd;

// Function prototypes
void sigchld_handler(int s); /* d�cr�menter les signaux */
int execOS400(char *cmd, char *result, int result_size, char *transform); /* ex�cution sur OS400 */
int checkIP(char *source, char *search); /* l'IP est-elle accept�e */
void trim(char * s); /* enlever les espaces avant et apr�s */
void update_threads(int inc); /* clean up before termination */
void handletimeout(int s); /* arrette si timeout d�pass� */
void sigchld_handler(int s); /* change le compteur nb_threads */

/* Execute une commande OS400
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *cmd Syntaxe � ex�cuter
* @param char *result R�sultat de la commande
* @param int result_size Taille du r�sultat
* @param char *transform Transformation de la syntaxe, par exemple "SYSTEM \"ACTCTL/%s\""
* @return int 0 si la commande s'est bien ex�cut�e, -1 si le code d'erreur n'est pas trouv� en prefixe du retour standard STDIN
*/
int execOS400(char *cmd, char *result, int result_size, char *transform) {
	char buf[2048]; // variable temporaire
    FILE* fp; // Descripteur de lecture de l'ex�cution de la commande
	bzero(result,result_size); // Clean du result
	snprintf(buf, sizeof buf, transform, cmd);
	printf("     [%d] Execution de la commande en : %s\n",getpid(),buf);
	
	// Execution de la commande
	fp = popen(buf,"r");
	fread(result,1,result_size,fp);
	fclose(fp);
	printf("     [%d] La commande a retourne :\n%s",getpid(),result);
	
	bzero(buf,sizeof(buf));
	strncpy(buf, (char *)result, 8);
	if (strcmp(buf,"CPD0030:")==0) {
		return -1;
	} else {
		return 0;
	}
}

/* Pr�dicat qui analyse si la chaine recherch�e est trouv�e dans la chaine source s�par�e par virgule
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *source Chaine � analyser, compos�e de sous chaines s�par�es par virgules
* @param char *search Chaine recherch�e
* @return int 0 si trouv�, -1 sinon
*/
int checkIP(char *source, char *search) {
	if (strcmp(source,"*all")==0) {
		return 0; // Toutes les IP sont accept�es
	}
	char tmp[strlen(source)+1]; // variable temporaire
	bzero(tmp,sizeof(source)); // Clean du result
	strcpy(tmp,source);
	char *p;
	p = strtok (tmp, ",");
	while (p != NULL){
		if (strcmp(p,search)==0) {
			return 0; // IP trouv�e
		}
		p = strtok (NULL, ",");
	}
	
	// Aucune IP accept�e correspondante � search
	return -1;
}

/* Supprime les espaces qui entourent une chaine
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *s Chaine � modifier
*/
void trim(char * s) {
    char * p = s;
    int l = strlen(p);
    while(isspace(p[l - 1])) p[--l] = 0;
    while(* p && isspace(* p)) ++p, --l;
    memmove(s, p, l + 1);
}

/* Change le nombre de threads
* @author Yann GAUTHERON <ygautheron@absystech.fr>
*/
void update_threads(int inc) {
	nb_threads+=inc;
	if (inc>0) {
		printf("--SERVEUR-- Nouveau thread occupe (%d en travail sur %d maximum)\n",nb_threads,max_threads);
	} else {
		printf("--SERVEUR-- Nouveau thread libre (%d en travail sur %d maximum)...\n",nb_threads,max_threads);
	}
}

/* Fonction qui veille aux signaux de fins de threads, permet de d�cr�menter le compteur
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param int s Signal
*/
void sigchld_handler(int s) {
	int status=0;
	if (childpid>0) {
    	while(waitpid(-1, NULL, WNOHANG) > 0);
		update_threads(-1);
	}
}

/* Ferme la connexion en cas de depassement du timeout
* @author Yann GAUTHERON <ygautheron@absystech.fr>
*/
void handletimeout(int s) {
	printf("     [%d] TIMEOUT ! Thread enfant depasse la limite de temps (%d secondes)\n",getpid(),timeout);
	write(newsockfd,"TLS8890: Time Out sur le service OS/400\n",40);
	close(newsockfd);
	exit(0);
}

/*
* Programme de serveur de socket
* @author Yann GAUTHERON <ygautheron@absystech.fr>
*/
int main(int argc, char *argv[]) {
	signal(SIGCHLD, SIG_IGN);
	
	int sockfd, portno,cli_portno;
	socklen_t clilen;
	char buffer[2048];
	struct sockaddr_in serv_addr, cli_addr;
	char ipstr[INET6_ADDRSTRLEN];
	int n;
    char result[42000]; // Buffer de r�sultat de la commande
	
	printf("Serveur d'exploitation OS400 - act400server v%s\n",version);
	printf("Pilotage par client distant a l'aide de 'act400rpc'\n");
	printf("ACT400 | copyright 1996-2012 | tous droits reserves\n\n");
	
	if (argc < 2) {
		fprintf(stderr,"ERREUR, aucun port fourni en parametre.\nUsage : %s port [max_threads] [ip_autorisee1[,ip_autorisee2[...]]] [timeout (en secondes)]\n",argv[0]);
		exit(1);
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERREUR ouverture de socket");
		exit(1);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	if (argc>=3) { // D�finition de la limite du nombre de threads
		max_threads=atoi(argv[2]);
	}
	printf("--SERVEUR-- Nombre maximum de thread = %d\n",max_threads);
	if (argc>=4 && strcmp(argv[3],"*all")!=0) { // Affichage des IP autoris�es
		printf("--SERVEUR-- IP autorisees = %s\n",argv[3]);
	} else {
		printf("--SERVEUR-- Toutes les IP sont autorisees\n",argv[3]);
	}
	if (argc>=5) { // D�finition de la limite de temps d'ex�cution
		timeout=atoi(argv[4]);
		printf("--SERVEUR-- Temps maximum par thread = %d secondes\n",timeout);
	} else {
		printf("--SERVEUR-- Temps maximum par thread illimite\n",timeout);
	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	printf("--SERVEUR-- Ecoute sur le port %i.\n",portno);
	while (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		printf("Le port est occupe, attente de quelques secondes...\n");
		sleep(5);
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
		printf("--SERVEUR-- En attente de clients...\n");
		
		// Calcul du nombre de threads disponibles
		if (nb_threads>=max_threads) {
			printf("--SERVEUR-- Maximum de threads atteint, attente d'un thread libre (%d en travail sur %d maximum)...\n",nb_threads,max_threads);
			wait(NULL);
		} else {
		
			newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
			if (newsockfd>=0) {
				switch(childpid=fork()){
					case -1://error
						fprintf(stderr, "Error spawning the child %d\n",errno);
						exit(0);
						
					case 0://thread enfant
						if (timeout>0) {
							signal(SIGALRM, handletimeout);
							alarm(timeout);
						}						
					
						printf("     [%d] Connexion ouverte enfant...\n",getpid());
						if (newsockfd < 0) {
							printf("     [%d] ERREUR accept socket !\n",getpid());
							perror("ERREUR accept socket !");
							exit(1);						
						}
						bzero(buffer,sizeof(buffer)); // Clean du buffer
						
						// R�ception du client
						getpeername(newsockfd, (struct sockaddr*)&cli_addr, &clilen);	
						struct sockaddr_in *s = (struct sockaddr_in *)&cli_addr;
						cli_portno = ntohs(s->sin_port);
						inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
						
						printf("     [%d] Socket client connecte, en attente de sa commande %s:%i...\n",getpid(),ipstr,cli_portno);
						
						// V�rification de l'IP accept�e
						if (argc<4 || checkIP(argv[3],ipstr)==0) {
							// Lecture du message
							n = read(newsockfd,buffer,sizeof(buffer)-1);
							if (n < 0) {
								perror("ERREUR lecture depuis socket !");
								exit(1);						
							}
							
							//printf("Avant trim :|%s|\n",buffer);
							trim(buffer);
							//printf("Apres trim :|%s|\n",buffer);
							//printf("Reception de la commande : %s\n",buffer);

							if (strstr(buffer,"ACTCTLSPE/")!=NULL) {
								// Prefixe forc�
								execOS400(buffer, (char *)&result, sizeof(result), (char *)transformForced);
							} else {
								// Si la commande �choue, on change le prefixe en ACTCTLSPE/
								if (execOS400(buffer, (char *)&result, sizeof(result), (char *)transformPrimary)!=0) {
									execOS400(buffer, (char *)&result, sizeof(result), (char *)transformSecondary);
								}
							}
							
							// Envoi au client
							printf("     [%d] Envoi du resultat au client\n",getpid());
							n = write(newsockfd,result,sizeof(result));
							if (n < 0) {
								perror("ERREUR ecriture vers socket !");
								exit(1);						
							}	
						} else {
							fprintf(stderr,"     [%d] ERREUR, adresse IP non autorisee.\n",getpid());
							write(newsockfd,"TLS8891: Requette OptimAct interdite depuis cette adresse IP\n",61); // Message pour informer le client de l'erreur
						}

						printf("     [%d] Connexion fermee enfant !\n",getpid());
						close(newsockfd);
						exit(0);

					default://serveur pere
						update_threads(1);
						printf("--SERVEUR-- Connexion fermee !\n");
						//close(newsockfd);
				}
			}
		}
	}
	close(sockfd);
	return 0; 
}