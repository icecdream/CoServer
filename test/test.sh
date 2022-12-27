#!/bin/bash  
  
for((i=1;i<=20000;i++));  
do   
	curl -i http://127.0.0.1:15678	
done  
