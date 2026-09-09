-- Public controlled setup; effects load unchanged installed card scripts.
Debug.ReloadFieldBegin(0)
Debug.SetPlayerInfo(0,8000,0,0)
Debug.SetPlayerInfo(1,8000,0,0)
for p=0,1 do
 for i=0,19 do Debug.AddCard(i%2==0 and 89631139 or 46986414,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE) end
end
Debug.AddCard(70368879,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK)
Debug.AddCard(37812118,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK)
Debug.ReloadFieldEnd()
local finish=Effect.GlobalEffect()
finish:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
finish:SetCode(EVENT_TO_GRAVE)
finish:SetCondition(function(e,tp,eg) return eg:IsExists(Card.IsCode,1,nil,37812118) end)
finish:SetOperation(function() Duel.Win(0,0) end)
Duel.RegisterEffect(finish,0)
