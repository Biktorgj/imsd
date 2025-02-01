
1. Service start
2. Connect to device
3. Query number of sims
  foreach sim ->
    NAS: Request RAT and current carrier ID
  <-- foreach end
  foreach sim
  	if RAT is LTE
  	  --> find correct PDC
  	  	-> if incorrect PDC is set, activate
  	  --> if no correct PDC1. Service start
2. Connect to device
3. Query number of sims
  foreach sim ->1. Service start
2. Connect to device
3. Query number of sims
  foreach sim ->
    NAS: Request RAT and current carrier ID
  <-- foreach end
  foreach sim
  	if RAT is LTE
  	  --> find correct PDC
  	  	-> if incorrect PDC is set, activate
  	  --> if no correct PDC is found
  	    -> Look for config files
  	    	--> if config file is found
  	    		-> Patch nvmem / efsmem [VIA MFS]
 <-- foreach end
 <--> Disconnect, reconnect <-->
 4. Query number of SIMs
   foreach sim ->
    Monitor RAT
    Sim 0 -> Subscription 0 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart
    Sim 1 -> Subscription 1 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart
    NAS: Request RAT and current carrier ID
  <-- foreach end
  foreach sim
  	if RAT is LTE
  	  --> find correct PDC
  	  	-> if incorrect PDC is set, activate
  	  --> if no correct PDC is found
  	    -> Look for config files
  	    	--> if config file is found
  	    		-> Patch nvmem / efsmem [VIA MFS]
 <-- foreach end
 <--> Disconnect, reconnect <-->
 4. Query number of SIMs
   foreach sim ->
    Monitor RAT
    Sim 0 -> Subscription 0 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart
    Sim 1 -> Subscription 1 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart is found
  	    -> Look for config files
  	    	--> if config file is found
  	    		-> Patch nvmem / efsmem [VIA MFS]
 <-- foreach end
 <--> Disconnect, reconnect <-->
 4. Query number of SIMs
   foreach sim ->
    Monitor RAT
    Sim 0 -> Subscription 0 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart
    Sim 1 -> Subscription 1 :: IMS Bind
      -> QRTR: DCM Service UP
      -> WDS bringup
       <-- On register
       	Watch connection handle
       		<-- On WDS stop
       			-> Restart